/* tools/m68k-rig/rig.c - Musashi wrapper that calls isolated 68000 functions.
 * Big-endian flat RAM, no chipset, no OS. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
#include "rig.h"

#ifndef RIG_M68KCONF
#error "Musashi did not pick up tools/m68k-rig/m68kconf.h (build with the Makefile's MUSASHI_CNF)"
#endif
#if M68K_EMULATE_ADDRESS_ERROR || M68K_INSTRUCTION_HOOK || M68K_EMULATE_PREFETCH || M68K_SEPARATE_READS || M68K_EMULATE_FC
#error "rig config options must all be OFF"
#endif

#define RIG_SENTINEL 0x00000100u   /* return address: an ILLEGAL opcode lives here */
#define RIG_STACK_TOP(mem) ((uint32_t)(mem) - 0x100u)

static uint8_t *ram;
static size_t ram_size;
static uint32_t max_instructions = 50000000u;
int rig_last_error;

void rig_set_max_instructions(uint32_t n) { max_instructions = n; }

static uint32_t rd(uint32_t a, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | (a + i < ram_size ? ram[a + i] : 0xFF);
    return v;
}
static void wr(uint32_t a, int n, uint32_t v) {
    for (int i = n - 1; i >= 0; i--) { if (a + i < ram_size) ram[a + i] = (uint8_t)(v & 0xFF); v >>= 8; }
}
/* Custom-register write log (rig.h): every CPU write to 0xDFF000-0xDFF1FF is appended while the log is on. */
#define IO_BASE 0xDFF000u
#define IO_END  0xDFF200u
static int io_log_on, io_read_zero;
static uint32_t io_log[RIG_IO_LOG_MAX][4];
static size_t io_log_n;
static uint32_t insn_count;
/* Audio interrupts (rig.h): a write of INTREQ that sets an audio bit, and a DMACON write that switches an audio channel on, raise that channel's interrupt, delivered
 * between instructions as a call of the handler with A1 = the channel's descriptor, the way the operating system's dispatcher does (SR and A1 are restored after). */
static int irq_on, irq_active, irq_dma[4];
static uint32_t irq_handler, irq_desc_tab, irq_pend, irq_saved_pc, irq_saved_a1, irq_saved_sr;
#define RIG_IRQ_RET 0x00000108u
static void irq_note(uint32_t a, int n, uint32_t v) {
    if (!irq_on || n < 2) return;
    if (n == 4 && a == IO_BASE + 0x096) v >>= 16;                 /* a long write also reaches the next register; only DMACON is watched */
    if (a == IO_BASE + 0x09C && (v & 0x8000)) irq_pend |= (v >> 7) & 0xF;
    else if (a == IO_BASE + 0x096)
        for (int ch = 0; ch < 4; ch++)
            if (v & (1u << ch)) {
                if (v & 0x8000) { if (!irq_dma[ch]) irq_pend |= 1u << ch; irq_dma[ch] = 1; }
                else irq_dma[ch] = 0;
            }
}
void rig_irq_audio(int on, uint32_t handler, uint32_t desc_tab) {
    irq_on = on != 0; irq_handler = handler; irq_desc_tab = desc_tab; irq_pend = 0; irq_active = 0;
    for (int i = 0; i < 4; i++) irq_dma[i] = 0;
}
void rig_irq_raise(int ch) { irq_pend |= 1u << (ch & 3); }
static void io_note(uint32_t a, int n, uint32_t v) {
    if (a >= IO_BASE && a < IO_END) irq_note(a, n, v);
    if (!io_log_on || a < IO_BASE || a >= IO_END) return;
    if (io_log_n < RIG_IO_LOG_MAX) {
        io_log[io_log_n][0] = m68k_get_reg(NULL, M68K_REG_PPC);
        io_log[io_log_n][1] = (a - IO_BASE) | ((uint32_t)n << 16);
        io_log[io_log_n][2] = v;
        io_log[io_log_n][3] = insn_count;
    }
    io_log_n++;
}
void   rig_io_log_set(int on) { io_log_on = on != 0; if (on) io_log_n = 0; }
size_t rig_io_log_get(uint32_t *buf, int max) {
    size_t n = io_log_n < RIG_IO_LOG_MAX ? io_log_n : RIG_IO_LOG_MAX;
    if (buf) for (size_t i = 0; i < n && (int)i < max; i++) memcpy(buf + 4 * i, io_log[i], sizeof io_log[i]);
    return io_log_n;
}
void rig_io_read_zero(int on) { io_read_zero = on != 0; }
static int io_zero(uint32_t a) { return io_read_zero && a >= IO_BASE && a < IO_END; }
unsigned int m68k_read_memory_8(unsigned int a)  { return io_zero(a) ? 0 : rd(a, 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return io_zero(a) ? 0 : rd(a, 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return io_zero(a) ? 0 : rd(a, 4); }
unsigned int m68k_read_disassembler_8(unsigned int a)  { return rd(a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return rd(a, 4); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { io_note(a, 1, v); wr(a, 1, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { io_note(a, 2, v); wr(a, 2, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { io_note(a, 4, v); wr(a, 4, v); }

int rig_init(size_t mem_size) {
    /* one global machine: a second init reuses or replaces the block instead of leaking it (a script that built a
     * rig per configuration grew by 14 MB each time and took the sandbox down) */
    if (ram && ram_size == mem_size) { memset(ram, 0, mem_size); }
    else { free(ram); ram = (uint8_t *)calloc(mem_size, 1); }
    if (!ram) return -1;
    ram_size = mem_size;
    wr(0, 4, RIG_STACK_TOP(mem_size));   /* reset SP */
    wr(4, 4, RIG_SENTINEL);              /* reset PC */
    wr(RIG_SENTINEL, 2, 0x4AFC);         /* ILLEGAL */
    wr(0x108, 2, 0x4AFC);                /* the audio interrupt's return marker */
    wr(0x110, 2, 0x4E75);                /* a bare RTS: rig_run(0x110) lets a raised interrupt run */
    irq_on = 0; irq_pend = 0; irq_active = 0;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    return 0;
}

int rig_load_image(const char *bin_path, uint32_t base) {
    if (!ram || base >= ram_size) return -1;
    FILE *f = fopen(bin_path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long len = ftell(f);
    if (len <= 0 || (size_t)len > ram_size - base || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t n = fread(ram + base, 1, (size_t)len, f);
    fclose(f);
    return n == (size_t)len ? 0 : -1;
}

void rig_poke(uint32_t addr, const uint8_t *src, size_t n) { memcpy(ram + addr, src, n); }
void rig_peek(uint32_t addr, uint8_t *dst, size_t n)       { memcpy(dst, ram + addr, n); }

/* One instruction per slice, so we stop exactly when RTS lands on the sentinel
 * and never execute the ILLEGAL opcode stored there. Sets rig_last_error. */
static uint32_t watch_addr[RIG_WATCH_MAX];
static int watch_n;
static uint32_t trace_buf[RIG_TRACE_MAX];
static size_t trace_n;

void rig_watch_set(const uint32_t *addrs, int n) {
    watch_n = n < 0 ? 0 : (n > RIG_WATCH_MAX ? RIG_WATCH_MAX : n);
    for (int i = 0; i < watch_n; i++) watch_addr[i] = addrs[i];
}
void   rig_trace_clear(void) { trace_n = 0; }
size_t rig_trace_get(uint32_t *out, size_t max) {
    size_t n = trace_n < max ? trace_n : max;
    if (out) memcpy(out, trace_buf, n * sizeof *out);
    return trace_n;
}

/* Call hooks (rig.h): when the pc reaches a hooked address, the callback runs and the routine is left as if it had executed RTS with D0 set. */
static struct { uint32_t addr; rig_hook_fn fn; uint32_t d0; } hooks[RIG_HOOK_MAX];
static int hook_n;
static int hook_has_d0; static uint32_t hook_d0;
void rig_hook_result(uint32_t d0) { hook_has_d0 = 1; hook_d0 = d0; }
void rig_hook_set(uint32_t addr, rig_hook_fn fn, uint32_t d0) {
    for (int i = 0; i < hook_n; i++) if (hooks[i].addr == addr) { hooks[i].fn = fn; hooks[i].d0 = d0; return; }
    if (hook_n < RIG_HOOK_MAX) { hooks[hook_n].addr = addr; hooks[hook_n].fn = fn; hooks[hook_n].d0 = d0; hook_n++; }
}
void rig_hooks_clear(void) { hook_n = 0; }

static void step_until_sentinel(void) {
    for (uint32_t n = 0; ; n++) {
        if (n >= max_instructions) { rig_last_error = RIG_ERR_TIMEOUT; return; }
        m68k_execute(1);
        insn_count++;
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        for (int i = 0; i < watch_n; i++)
            if (pc == watch_addr[i]) { if (trace_n < RIG_TRACE_MAX) trace_buf[trace_n] = pc; trace_n++; break; }
        if (irq_on) {
            if (irq_active && pc == RIG_IRQ_RET) {
                m68k_set_reg(M68K_REG_A1, irq_saved_a1); m68k_set_reg(M68K_REG_SR, irq_saved_sr);
                m68k_set_reg(M68K_REG_PC, irq_saved_pc); pc = irq_saved_pc; irq_active = 0;
            }
            if (irq_pend && !irq_active) {
                int ch = 0; while (!(irq_pend & (1u << ch))) ch++;
                irq_pend &= ~(1u << ch);
                irq_saved_pc = pc; irq_saved_a1 = m68k_get_reg(NULL, M68K_REG_A1); irq_saved_sr = m68k_get_reg(NULL, M68K_REG_SR); irq_active = 1;
                uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP) - 4;
                wr(sp, 4, RIG_IRQ_RET);
                m68k_set_reg(M68K_REG_SP, sp);
                m68k_set_reg(M68K_REG_A1, rd(irq_desc_tab + 4 * (uint32_t)ch, 4));
                m68k_set_reg(M68K_REG_PC, irq_handler); pc = irq_handler;
            }
        }
        if (pc == RIG_SENTINEL) return;
        for (int i = 0; i < hook_n; i++)
            if (pc == hooks[i].addr) {
                uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
                hook_has_d0 = 0;
                if (hooks[i].fn) hooks[i].fn();
                m68k_set_reg(M68K_REG_D0, hook_has_d0 ? hook_d0 : hooks[i].d0);
                m68k_set_reg(M68K_REG_PC, rd(sp, 4));
                m68k_set_reg(M68K_REG_SP, sp + 4);
                pc = m68k_get_reg(NULL, M68K_REG_PC);
                break;
            }
        if (pc == RIG_SENTINEL) return;
        if (pc >= ram_size) { rig_last_error = RIG_ERR_UNEXPECTED_PC; return; }
    }
}

uint32_t rig_call(uint32_t addr, const uint32_t *args, int nargs) {
    rig_last_error = RIG_OK;
    insn_count = 0;
    if (!ram) { rig_last_error = RIG_ERR_UNEXPECTED_PC; return 0; }
    uint32_t sp = RIG_STACK_TOP(ram_size);
    for (int i = nargs - 1; i >= 0; i--) { sp -= 4; wr(sp, 4, args[i]); }
    sp -= 4; wr(sp, 4, RIG_SENTINEL);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, addr);
    step_until_sentinel();
    return rig_last_error == RIG_OK ? m68k_get_reg(NULL, M68K_REG_D0) : 0;
}

static int musashi_reg(rig_reg_t r) {
    static const int map[] = { M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
                               M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
                               M68K_REG_PC, M68K_REG_SR };
    return map[r];
}
uint32_t rig_get_reg(rig_reg_t r) { return m68k_get_reg(NULL, musashi_reg(r)); }
void     rig_set_reg(rig_reg_t r, uint32_t v) { m68k_set_reg(musashi_reg(r), v); }

int rig_run(uint32_t addr) {
    rig_last_error = RIG_OK;
    insn_count = 0;
    if (!ram || addr >= ram_size) { rig_last_error = RIG_ERR_UNEXPECTED_PC; return -1; }
    uint32_t sp = RIG_STACK_TOP(ram_size) - 4;
    wr(sp, 4, RIG_SENTINEL);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, addr);
    step_until_sentinel();
    return rig_last_error == RIG_OK ? 0 : -1;
}

void rig_shutdown(void) { free(ram); ram = NULL; ram_size = 0; }

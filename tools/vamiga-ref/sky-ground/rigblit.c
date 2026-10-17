/* Provenance and licence: the blitter model below (do_blit, the line logic, the fill, the minterm and barrel shifter helpers) is a port to C of the logic of vAmiga's
 * FastBlitter and Blitter::doLine (https://github.com/dirkwhoffmann/vAmiga, Core/Components/Agnus/Blitter), which is licensed under the GNU GPL version 3.0.
 * It is therefore a derivative work licensed GPL-3.0-or-later. The repository licence is the owner's decision; until it is settled this file (and the
 * library built from it) must be treated as GPL-3.0 code. The Musashi wrapper part is from tools/m68k-rig/rig.c. Validated by the frame comparisons of
 * docs/re/04-scene-composition.md (the checks of the models against this rig are model-against-model) and, for the per-channel modulus in descending
 * mode (BLTCMOD applied to C even where it differs from the other channels' modulus, as in the alternate blit 0x34EEC), by the C-modulus probe of note 04
 * "Stage [90]" (nearground_blit_check.py: 38 blits in vAmiga's SlowBlitter, C end pointer and pixels). */
/* tools/vamiga-ref/sky-ground/rigblit.c: the Musashi rig of tools/m68k-rig/rig.c (same API) with two changes for running the sky-fill and
 * ground-polygon code of docs/re/04-scene-composition.md outside the emulator:
 *   - 16 MiB of flat RAM are expected (rig_init(1 << 24)), so the custom-chip block at 0xDFF000 is plain memory and the code's wait on
 *     DMACONR (reads zero) never spins;
 *   - a write to BLTSIZE (0xDFF058) runs the blit at once with a small OCS blitter model (area blits with fill, descending mode, line mode
 *     with one-dot and the XOR line minterm). The model follows the behaviour of vAmiga's fast blitter (checked against it by the frame
 *     comparisons in check_frames.py); pointer and modulo bit 0 are ignored as on the chip.
 * Build: bash tools/vamiga-ref/sky-ground/build.sh. Not a replacement for tools/m68k-rig (no trace hooks beyond the pc watch). */
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

/* ---- scratch blitter model (OCS), ported from vAmiga FastBlitter ---- */
#define CH 0xDFF000u
static int blt_log;
static uint16_t r16(uint32_t a){ a&=0xFFFFFF; return a+1<ram_size?(ram[a]<<8)|ram[a+1]:0; }
static void w16(uint32_t a,uint16_t v){ a&=0xFFFFFF; if(a+1<ram_size){ram[a]=v>>8;ram[a+1]=v;} }
static uint32_t r32(uint32_t a){ return ((uint32_t)r16(a)<<16)|r16(a+2); }
static void w32(uint32_t a,uint32_t v){ w16(a,v>>16); w16(a+2,v); }
unsigned long blit_count, line_count;
static uint16_t minterm(uint16_t a,uint16_t b,uint16_t c,int m){
  uint16_t r=0;
  if(m&0x80)r|=a&b&c; if(m&0x40)r|=a&b&~c; if(m&0x20)r|=a&~b&c; if(m&0x10)r|=a&~b&~c;
  if(m&0x08)r|=~a&b&c; if(m&0x04)r|=~a&b&~c; if(m&0x02)r|=~a&~b&c; if(m&0x01)r|=~a&~b&~c;
  return r; }
static uint16_t barrel(uint16_t n,uint16_t o,int sh,int desc){
  uint32_t v; if(desc){ v=((uint32_t)n<<16)|o; return (uint16_t)(v>>(16-sh)); } v=((uint32_t)o<<16)|n; return (uint16_t)(v>>sh); }
static void fillw(uint16_t *d,int *carry,int excl){
  uint16_t in=*d,out=in; int c=*carry;
  for(int bit=0;bit<16;bit++){ if(excl) out^=(uint16_t)(c<<bit); else out|=(uint16_t)(c<<bit); if(in&(1<<bit)) c=!c; }
  *d=out; *carry=c; }
static void do_blit(void){
  uint16_t con0=r16(CH+0x40),con1=r16(CH+0x42),size=r16(CH+0x58);
  uint16_t fwm=r16(CH+0x44),lwm=r16(CH+0x46);
  int H=size>>6,W=size&0x3f; if(!H)H=1024; if(!W)W=64;
  uint32_t apt=r32(CH+0x50)&~1u,bpt=r32(CH+0x4C)&~1u,cpt=r32(CH+0x48)&~1u,dpt=r32(CH+0x54)&~1u;
  int16_t amod=r16(CH+0x64)&0xFFFE,bmod=r16(CH+0x62)&0xFFFE,cmod=r16(CH+0x60)&0xFFFE,dmod=r16(CH+0x66)&0xFFFE;
  uint16_t adat=r16(CH+0x74),bdat=r16(CH+0x72),cdat=r16(CH+0x70);
  int useA=con0&0x800,useB=con0&0x400,useC=con0&0x200,useD=con0&0x100;
  if(blt_log) fprintf(stderr,"BLIT con0=%04x con1=%04x size=%dx%d A=%x B=%x C=%x D=%x mods a%d b%d c%d d%d\n",con0,con1,W,H,apt,bpt,cpt,dpt,amod,bmod,cmod,dmod);
  if(con1&1){ /* line */
    line_count++;
    int sing=con1&2, sign=(con1&0x40)!=0; int ash=con0>>12, bsh=con1>>12; int first=1;
    uint16_t bnew=bdat,chold=cdat; uint16_t anew=adat;
    for(int i=0;i<H;i++){
      if(useB){ bnew=r16(bpt); bpt+=bmod; }
      if(useC) chold=r16(cpt);
      uint16_t ah=(uint16_t)((uint32_t)(anew&fwm)>>ash);
      uint16_t bh=(uint16_t)((((uint32_t)bnew<<16)|bnew)>>bsh);
      if(bsh--==0)bsh=15;
      uint16_t d=minterm(ah,(bh&1)?0xFFFF:0,chold,con0&0xFF);
      int we=(!sing||first)&&useC;
      first=0;
      #define INCX() do{ if(++ash==16){ash=0;cpt+=2;} }while(0)
      #define DECX() do{ if(ash--==0){ash=15;cpt-=2;} }while(0)
      #define INCY() do{ cpt+=cmod; first=1; }while(0)
      #define DECY() do{ cpt-=cmod; first=1; }while(0)
      if(!sign){ if(con1&0x10){ if(con1&8)DECY(); else INCY(); } else { if(con1&8)DECX(); else INCX(); } }
      if(con1&0x10){ if(con1&4)DECX(); else INCX(); } else { if(con1&4)DECY(); else INCY(); }
      if(useA){ apt+= sign?bmod:amod; }
      sign=(int16_t)apt<0;
      if(we) w16(dpt,d);
      dpt=cpt;
    }
    w32(CH+0x50,apt);w32(CH+0x4C,bpt);w32(CH+0x48,cpt);w32(CH+0x54,dpt);
    return; }
  blit_count++;
  int desc=(con1&2)!=0, fill=(con1&0x18)!=0, excl=(con1&0x10)!=0, fci=(con1&4)!=0;
  int incr=desc?-2:2; int32_t am=desc?-amod:amod,bm=desc?-bmod:bmod,cm=desc?-cmod:cmod,dm=desc?-dmod:dmod;
  uint16_t aold=0,bold=0,anew=adat,bnew=bdat,chold=cdat;
  int ash=con0>>12,bsh=con1>>12;
  for(int y=0;y<H;y++){
    int carry=fci; uint16_t mask=fwm;
    for(int x=0;x<W;x++){
      if(x==W-1) mask&=lwm;
      if(useA){anew=r16(apt);apt+=incr;}
      if(useB){bnew=r16(bpt);bpt+=incr;}
      if(useC){chold=r16(cpt);cpt+=incr;}
      uint16_t ah=barrel(anew&mask,aold,ash,desc); aold=anew&mask;
      uint16_t bh=bnew; if(useB){ bh=barrel(bnew,bold,bsh,desc); bold=bnew; }
      uint16_t d=minterm(ah,bh,chold,con0&0xFF);
      if(fill) fillw(&d,&carry,excl);
      if(useD){w16(dpt,d);dpt+=incr;}
      mask=0xFFFF; }
    if(useA)apt+=am; if(useB)bpt+=bm; if(useC)cpt+=cm; if(useD)dpt+=dm; }
  w32(CH+0x50,apt);w32(CH+0x4C,bpt);w32(CH+0x48,cpt);w32(CH+0x54,dpt);
}
void rig_blit_log(int on){ blt_log=on; }

unsigned int m68k_read_memory_8(unsigned int a)  { return rd(a, 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd(a, 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return rd(a, 4); }
unsigned int m68k_read_disassembler_8(unsigned int a)  { return rd(a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return rd(a, 4); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { wr(a, 1, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { wr(a, 2, v); if(a==0xDFF058u) do_blit(); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { wr(a, 4, v); }

int rig_init(size_t mem_size) {
    free(ram);                           /* a second rig_init frees the previous rig's RAM (each stage() used to leak 16 MiB) */
    ram = (uint8_t *)calloc(mem_size, 1);
    if (!ram) return -1;
    ram_size = mem_size;
    wr(0, 4, RIG_STACK_TOP(mem_size));   /* reset SP */
    wr(4, 4, RIG_SENTINEL);              /* reset PC */
    wr(RIG_SENTINEL, 2, 0x4AFC);         /* ILLEGAL */
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

static void step_until_sentinel(void) {
    for (uint32_t n = 0; ; n++) {
        if (n >= max_instructions) { rig_last_error = RIG_ERR_TIMEOUT; return; }
        m68k_execute(1);
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        for (int i = 0; i < watch_n; i++)
            if (pc == watch_addr[i]) { if (trace_n < RIG_TRACE_MAX) trace_buf[trace_n] = pc; trace_n++; break; }
        if (pc == RIG_SENTINEL) return;
        if (pc >= ram_size) { rig_last_error = RIG_ERR_UNEXPECTED_PC; return; }
    }
}

uint32_t rig_call(uint32_t addr, const uint32_t *args, int nargs) {
    rig_last_error = RIG_OK;
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
    if (!ram || addr >= ram_size) { rig_last_error = RIG_ERR_UNEXPECTED_PC; return -1; }
    uint32_t sp = RIG_STACK_TOP(ram_size) - 4;
    wr(sp, 4, RIG_SENTINEL);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, addr);
    step_until_sentinel();
    return rig_last_error == RIG_OK ? 0 : -1;
}

void rig_shutdown(void) { free(ram); ram = NULL; ram_size = 0; }

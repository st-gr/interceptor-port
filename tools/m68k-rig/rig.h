#ifndef INTERCEPTOR_RIG_H
#define INTERCEPTOR_RIG_H
#include <stddef.h>
#include <stdint.h>
int      rig_init(size_t mem_size);
int      rig_load_image(const char *bin_path, uint32_t base);
void     rig_poke(uint32_t addr, const uint8_t *src, size_t n);
void     rig_peek(uint32_t addr, uint8_t *dst, size_t n);
uint32_t rig_call(uint32_t addr, const uint32_t *args, int nargs);
void     rig_shutdown(void);

typedef enum { RIG_D0 = 0, RIG_D1, RIG_D2, RIG_D3, RIG_D4, RIG_D5, RIG_D6, RIG_D7,
               RIG_A0, RIG_A1, RIG_A2, RIG_A3, RIG_A4, RIG_A5, RIG_A6, RIG_A7,
               RIG_PC, RIG_SR } rig_reg_t;
uint32_t rig_get_reg(rig_reg_t r);
void     rig_set_reg(rig_reg_t r, uint32_t v);
/* Run from addr until its RTS returns to the sentinel, with registers as set.
 * Returns 0 on success, else -1 with rig_last_error set. */
int      rig_run(uint32_t addr);

/* rig_call sets this: 0 ok; on failure it returns 0 with the error set. */
enum { RIG_OK = 0, RIG_ERR_TIMEOUT = 1, RIG_ERR_UNEXPECTED_PC = 2 };
extern int rig_last_error;
void     rig_set_max_instructions(uint32_t n);   /* default 50,000,000 */

/* Program-counter watch (a trace of control-flow landmarks): after every instruction the pc is compared with the watched addresses; each hit appends the
 * address to a trace (up to RIG_TRACE_MAX are kept, hits beyond that are only counted). rig_watch_set with n = 0 disables the watch. rig_trace_get copies
 * up to max entries and returns the total number of hits since rig_trace_clear. */
#define RIG_WATCH_MAX 128
#define RIG_TRACE_MAX 65536
void   rig_watch_set(const uint32_t *addrs, int n);
void   rig_trace_clear(void);
size_t rig_trace_get(uint32_t *out, size_t max);

/* Call hooks: rig_hook_set makes the routine at addr a stub: when the pc reaches it the callback fn runs (it may rig_peek/rig_get_reg; fn may be NULL) and
 * the routine returns at once with D0 = d0, as if it had executed RTS (the return address is popped from the stack). Used to replace a callee by a
 * logger. rig_hooks_clear removes them all. */
#define RIG_HOOK_MAX 16
typedef void (*rig_hook_fn)(void);
void rig_hook_set(uint32_t addr, rig_hook_fn fn, uint32_t d0);
void rig_hooks_clear(void);
/* Called from inside a hook callback: the routine returns d0 instead of the value given to rig_hook_set. */
void rig_hook_result(uint32_t d0);

/* Custom-register write log: while on, every CPU write to 0xDFF000-0xDFF1FF is appended as four words (pc of the writing instruction, register offset from
 * 0xDFF000 with the access size in bytes in bits 16 and up, the value written, the number of instructions executed since the call or run began).
 * rig_io_log_set(1) clears the log; the log keeps RIG_IO_LOG_MAX entries (hits beyond that are only counted). rig_io_log_get copies up to max entries
 * (four words each) into buf and returns the total number of writes logged. rig_io_read_zero(1) makes every CPU read of 0xDFF000-0xDFF1FF return 0
 * (the chipset's status registers that the sound loader may poll); off (default) the range is plain RAM. */
#define RIG_IO_LOG_MAX 65536
void   rig_io_log_set(int on);
size_t rig_io_log_get(uint32_t *buf, int max);
void   rig_io_read_zero(int on);

/* Audio interrupts: rig_irq_audio(1, handler, desc_tab) makes the rig deliver the audio interrupt of channel ch (0-3) by calling the handler with A1 = the long at
 * desc_tab + 4 * ch, between instructions, when the game writes INTREQ with SETCLR and the channel's bit (0x80 << ch) or switches the channel on in DMACON (a
 * channel that was off); SR and A1 are restored on return. rig_irq_raise(ch) asks for one by hand (the end of a sample block); it runs at the next
 * instruction (rig_run(0x110) is a bare RTS for that). rig_irq_audio(0, 0, 0) turns it off. */
void rig_irq_audio(int on, uint32_t handler, uint32_t desc_tab);
void rig_irq_raise(int ch);
#endif

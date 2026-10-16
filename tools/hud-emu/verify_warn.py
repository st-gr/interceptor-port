"""Warning line: runs the original's tick generator (0x13C94), arbiter (0x13D4C) and message driver (0x36DB2) frame by frame
over snapshot memory, prints the code word and the shown entry, and asserts the rules of note 06 ("Warning line").

Usage (from the repository root):
  python3 tools/hud-emu/verify_warn.py              run the checks (exit 0 = all pass, 1 = a check failed)
  python3 tools/hud-emu/verify_warn.py NAME ...     print the frame log of the named scenarios (see SCEN)
  python3 tools/hud-emu/verify_warn.py --all        print every scenario log, then run the checks

Inputs (git-ignored, under extracted/):
  interceptor.bin, interceptor.map.json              relocated image (tools/hunk/relocate.py)
  captures/cockpit-ground.vasnap                     RAM for every scenario and check
  captures/*.vasnap, captures/views/*.vasnap         every flight snapshot (V:4A84D nonzero) for the phase relation check
Missing interceptor.bin or cockpit-ground.vasnap: the script prints SKIP and exits 0.

One frame = generator, arbiter, driver in the order of the frame routine (0x1113A, 0x1127C, 0x113D0). Scenarios hold a
condition word (V:4AC08), threat bits (V:4A926) or a posted code for some frames.
"""
import os, sys, struct, glob, io, contextlib
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_need = ['extracted/interceptor.bin', 'extracted/interceptor.map.json', 'extracted/captures/cockpit-ground.vasnap']
_missing = [p for p in _need if not os.path.exists(os.path.join(ROOT, p))]
if _missing and __name__ == '__main__':
    print('SKIP verify_warn: missing ' + ', '.join(_missing))
    sys.exit(0)
from util import *

GEN, ARB, DRV, POST = 0x13c94, 0x13d4c, 0x36db2, 0x29bc8
TABLE = 0x41ff0

def table_text(e, idx):
    a = e.R(TABLE + 28 * idx + 1)
    return bytes(e.uc.mem_read(a, 26)).decode('latin1')

def shown_entry(e):
    """index of the entry whose text is in the driver's buffer V:4A8C2 (None if no match)"""
    buf = bytes(e.uc.mem_read(e.R(0x4a8c2), 26)).decode('latin1')
    for i in range(54):
        if table_text(e, i) == buf:
            return i
    return None

def post(e, code):
    """call the post routine 0x29BC8 with the code word in D0"""
    uc = e.uc
    from unicorn.m68k_const import UC_M68K_REG_D0
    sp = 0xbf0f00
    uc.mem_write(sp - 4, struct.pack('>I', 0xbf0000)); sp -= 4
    from unicorn.m68k_const import UC_M68K_REG_A7
    uc.reg_write(UC_M68K_REG_A7, sp)
    for r in range(8): uc.reg_write(UC_M68K_REG_D0 + r, 0)
    uc.reg_write(UC_M68K_REG_D0, code)
    e.n = 0; e.maxi = 100000; e.err = None
    uc.emu_start(e.R(POST), 0xbf0000, count=100000)

def fresh(base='cockpit-ground', gen_phase=None):
    e = Emu3(SNAP % base)
    for p in e.pl[:4]: e.uc.mem_write(p, bytes(8000))
    for a in (0x4a984, 0x4a986, 0x4a98e, 0x4ab96, 0x4ab98, 0x4ab9a, 0x4ab9c, 0x4aa7c):
        e.w(a, 0)
    for a in (0x4a926, 0x4a946, 0x4a94a, 0x4a94b, 0x4a8f4, 0x4a91a, 0x4a91b):
        e.b(a, 0)
    e.l(0x4ac08, 0); e.w(0x4aa78, -1)
    e.b(0x4a984 + 1, 0x40)       # V:4A985 bit 6: simulation running
    if gen_phase is not None: e.b(0x4a948, gen_phase)
    return e

def frame(e):
    e.call(GEN); g = (e.rb(0x4a948), e.rb(0x4a946))
    e.call(ARB)
    e.call(DRV)
    return g

def run(e, nframes, script=None, label=''):
    """script(frame_number, e) sets the inputs before each frame; returns the printed rows"""
    rows = []
    for k in range(nframes):
        if script: script(k, e)
        cnt, bits = frame(e)
        rows.append((k, cnt, bits, e.rw(0x4ab98) & 0xffff, e.rw(0x4ab96) & 0xffff, e.rw(0x4ab9a) & 0xffff,
                     e.rb(0x4a94a), e.rw(0x4a986) & 0x2020, e.rw(0x4a984) & 0xff, shown_entry(e)))
    print('== ' + label)
    print('frame gen946 code     line96  prev9A  counter b13/b5 984lo shown')
    for r in rows:
        print('%4d  %d/%02x   %04x    %04x    %04x    %3d      %04x   %02x     %s' % (
            r[0], r[1], r[2], r[3], r[4], r[5], r[6] if r[6] < 128 else r[6] - 256, r[7], r[8], r[9]))
    return rows

def cond(frames, ac08=0, t926=0):
    def s(k, e):
        on = frames[0] <= k < frames[1]
        e.l(0x4ac08, ac08 if on else 0); e.b(0x4a926, t926 if on else 0)
    return s

def posted(frame, code, extra=None):
    def s(k, e):
        if k == frame: post(e, code)
        if extra: extra(k, e)
    return s

def paused(frames, inner):
    def s(k, e):
        inner(k, e)
        e.b(0x4a866, 1 if frames[0] <= k < frames[1] else 0)
    return s

def switch(frame, a, b):
    def s(k, e):
        if k < frame: e.l(0x4ac08, a[0]); e.b(0x4a926, a[1])
        else: e.l(0x4ac08, b[0]); e.b(0x4a926, b[1])
    return s

SCEN = {
    'switch-alert': lambda: (fresh(gen_phase=8), 30, switch(4, (2, 0), (0, 0x10))),
    'switch-alert2': lambda: (fresh(gen_phase=8), 30, switch(7, (2, 0), (0, 0x10))),
    'pause-alert': lambda: (fresh(gen_phase=8), 50, paused((10, 30), cond((2, 50), 0x02, 0))),
    'post-4005':  lambda: (fresh(gen_phase=8), 50, posted(2, 0x4005)),
    'post-d00a':  lambda: (fresh(gen_phase=8), 50, posted(2, 0xd00a)),
    'post-401d':  lambda: (fresh(gen_phase=8), 20, posted(2, 0x401d)),
    'post-over-alert': lambda: (fresh(gen_phase=8), 60, posted(20, 0x4005, cond((2, 60), 0x80, 0))),
    # arbiter alert: IR missile locked on the player (entry 16, code 0x8010, D = 8, slow blink, partner entry 17)
    'alert-ir':   lambda: (fresh(gen_phase=8), 40, cond((2, 40), 0, 0x10)),
    # alert with duration 6: fuel critical (entry 2, flag 0x86)
    'alert-fuel2': lambda: (fresh(gen_phase=8), 80, cond((2, 80), 0x02, 0)),
    # timed alert and info: stall (0xC001)
    'stall':      lambda: (fresh(gen_phase=8), 60, cond((2, 60), 0x80, 0)),
    # timed information from the arbiter (friendly nearby 0x400D), condition held then released
    'friendly':   lambda: (fresh(gen_phase=8), 90, cond((2, 70), 0, 0x02)),
    # plain fuel low 0x0C09
    'fuellow':    lambda: (fresh(gen_phase=8), 30, cond((2, 20), 0x01, 0)),
}

# ---------------------------------------------------------------------------------------------------------------------
# checks
IMGB = open(ROOT + '/extracted/interceptor.bin', 'rb').read()
fails = 0


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('  ' + str(detail) if detail and not ok else ''))
    fails += 0 if ok else 1


def words(a, n):
    return IMGB[a - 0x10000:a - 0x10000 + n].hex()


def quiet_run(e, n, sc):
    with contextlib.redirect_stdout(io.StringIO()):
        return run(e, n, sc, '')


def expected_tick(counter):
    """tick word V:4A946 left by the generator for the counter value after the decrement (the arbiter clears it later)"""
    return {8: 0x86, 4: 0x06, 6: 0x04, 2: 0x04}.get(counter, 0)


def check_structure():
    """where the generator, arbiter and the frame counter are called and stored (the claims of the scope paragraph)"""
    check('frame routine 0x11124 calls the generator at 0x1113A before the V:4A84D test at 0x11140',
          words(0x1113a, 6) == '4eb900013c94' and words(0x11140, 6) == '4a390004a84d' and words(0x11146, 2) == '6700')
    beq_target = 0x11148 + int(words(0x11148, 2), 16)
    check('V:4A84D zero branches to 0x114C0 (the idle path)', beq_target == 0x114c0, hex(beq_target))
    check('idle path 0x114C0..0x114D0 does not call the arbiter', '4eb900013d4c' not in words(0x114c0, 0x10))
    calls, i = [], -1
    pat = bytes.fromhex('4eb900013d4c')
    while True:
        i = IMGB.find(pat, i + 1)
        if i < 0: break
        calls.append(i + 0x10000)
    check('the arbiter has one caller, 0x1127C, on the flight path (join point 0x11274)', calls == [0x1127c] and 0x11274 < 0x1127c < 0x11282)
    check('the main loop calls the frame routine every pass (jsr 0x11124 at 0x17EF2)', words(0x17ef2, 6) == '4eb900011124')
    check('frame counter: pause test 0x114A6 (V:4A866), read 0x114B0, increment 0x114B6, write 0x114B8',
          words(0x114a6, 6) == '10390004a866' and words(0x114ae, 2) == '6620' and words(0x114b0, 6) == '30390004a992'
          and words(0x114b6, 2) == '5240' and words(0x114b8, 6) == '33c00004a992')
    check('n is cleared (clr.w) at 0x12EC0 and 0x138B6; the pause key handler clears its low three bits at 0x1EE00',
          words(0x12ec0, 6) == '42790004a992' and words(0x138b6, 6) == '42790004a992' and words(0x1ee00, 8) == '0279fff80004a992')


def check_seeded_sequence():
    """the reference-path pin: after the first generator pass the counter is 8 - ((n0 + 3) mod 8); the generator then runs free"""
    for n0 in range(8):
        target = 8 - ((n0 + 3) % 8)
        seed = target + 1 if target < 8 else 1          # the counter after the decrement of the first pass equals target
        e = fresh(gen_phase=seed)
        seq = []
        for k in range(24):
            e.w(0x4a992, (n0 + k) & 0xffff)             # the generator does not read n
            e.b(0x4a866, 1 if k % 5 == 0 else 0)        # nor the pause byte
            e.b(0x4a84d, k % 2)                         # nor V:4A84D (the gate is in the caller)
            e.call(GEN)
            seq.append((e.rb(0x4a948), e.rb(0x4a946)))
            e.b(0x4a946, 0)                             # the arbiter's clear
        want = [(8 - ((n0 + k + 3) % 8), expected_tick(8 - ((n0 + k + 3) % 8))) for k in range(24)]
        check('seeded generator, n0 = %d: counter %d after the first pass, then free-running' % (n0, target), seq == want, (seq[:8], want[:8]))
    e = fresh(gen_phase=6)
    seq = []
    for k in range(6):
        e.call(GEN); seq.append((e.rb(0x4a948), e.rb(0x4a946))); e.b(0x4a946, 0)
    check('n0 = 0 (n cleared at the start): counters 5, 4, 3, 2, 1, 8 with ticks 0, 0x06, 0, 0x04, 0, 0x86',
          seq == [(5, 0), (4, 6), (3, 0), (2, 4), (1, 0), (8, 0x86)], seq)
    # slow tick = bit 1, fast tick = bit 2: slow every 4 passes, fast every 2
    e = fresh(gen_phase=1)
    slow, fast = [], []
    for k in range(32):
        e.call(GEN); t = e.rb(0x4a946); slow.append(bool(t & 2)); fast.append(bool(t & 4)); e.b(0x4a946, 0)
    check('slow tick every 4th pass, fast tick every 2nd', [i for i, v in enumerate(slow) if v] == list(range(0, 32, 4)) and [i for i, v in enumerate(fast) if v] == list(range(0, 32, 2)))


def check_snapshot_phase():
    """the counter against n in every flight snapshot: counter = 8 - ((n_pre + 3) mod 8) with n_pre = n - 1 (the snapshot is taken after the increment)"""
    files = sorted(glob.glob(ROOT + '/extracted/captures/*.vasnap') + glob.glob(ROOT + '/extracted/captures/views/*.vasnap'))
    n_ok, dev = 0, []
    for f in files:
        try:
            e = Emu3(f)
        except Exception:
            continue
        if e.rb(0x4a84d) == 0:
            continue
        n = e.rw(0x4a992) & 0xffff
        if e.rb(0x4a948) == 8 - ((n + 2) % 8): n_ok += 1
        else: dev.append(os.path.basename(f))
    if not n_ok + len(dev):
        print('skip snapshot phase relation: no flight snapshot found'); return
    check('flight snapshots: counter = 8 - ((n + 2) mod 8) in all but at most one (%d agree, deviating: %s)' % (n_ok, ', '.join(dev) or 'none'),
          len(dev) <= 1 and n_ok >= 1, dev)


def shown_passes(code, phase, nf=90):
    """passes at whose end the line shows the entry, for a code posted before pass 2 with the generator seeded at `phase`"""
    e = fresh(gen_phase=phase)
    rows = quiet_run(e, nf, posted(2, code))
    return sum(1 for r in rows if (r[4] & 0xff) == (code & 0xff))


def check_durations():
    d8 = sorted(set(shown_passes(0x400d, ph) for ph in range(1, 9)))
    check('timed information D = 8 (entry 13) is shown for 32 to 35 passes depending on the phase of the post', d8 == [32, 33, 34, 35], d8)
    d0 = sorted(set(shown_passes(0x401d, ph) for ph in range(1, 9)))
    check('timed information D = 0 (entry 29) is shown for 0 to 3 passes depending on the phase of the post', d0 == [0, 1, 2, 3], d0)
    e = fresh(gen_phase=8)
    rows = quiet_run(e, 90, cond((2, 90), 0x02, 0))
    code = [r[3] for r in rows]
    starts = sum(1 for k in range(1, 51) if rows[k][9] == 2 and rows[k - 1][9] != 2)
    check('alert D = 6 (entry 2, fuel critical): code 0x8002 for 49 passes with 6 text phases, then the bare index 2 (steady)',
          code[2:51] == [0x8002] * 49 and code[51:] == [0x0002] * (len(code) - 51) and starts == 6, (code[48:54], starts))
    e = fresh(gen_phase=8)
    rows = quiet_run(e, 40, cond((2, 40), 0, 0x10))
    check('alert D = 8 (entry 16, IR missile): text phase 4 passes, partner entry 17 for 4 passes, counter 8 after the load',
          [r[9] for r in rows[3:11]] == [16, 16, 16, 16, 17, 17, 17, 17] and rows[2][6] == 8, [r[9] for r in rows[3:11]])
    e = fresh(gen_phase=8)
    rows = quiet_run(e, 30, posted(2, 0xd00a))
    check('posted 0xD00A (D = 0, fast): blinks 2 passes on, 2 off with no expiry in 28 passes', all(r[3] == 0xd00a for r in rows[2:]))


def check_pause():
    e = fresh(gen_phase=8)
    rows = quiet_run(e, 50, paused((10, 30), cond((2, 50), 0x02, 0)))
    steady = all(rows[k][9] == 2 for k in range(12, 30))
    check('pause: the entry-2 alert shows its steady text during the pause (no partner frames)', steady, [rows[k][9] for k in range(10, 30)])


if __name__ == '__main__':
    args = sys.argv[1:]
    show_all = '--all' in args
    names = [a for a in args if not a.startswith('--')]
    if show_all: names = list(SCEN)
    for nm in names:
        e, n, sc = SCEN[nm]()
        run(e, n, sc, nm)
    if names and not show_all:
        sys.exit(0)
    check_structure()
    check_seeded_sequence()
    check_snapshot_phase()
    check_durations()
    check_pause()
    print('failures', fails)
    sys.exit(1 if fails else 0)

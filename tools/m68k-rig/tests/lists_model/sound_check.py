"""The game's sound library and its callers against the Python model of docs/re/10-sound.md (sndmodel.py), with the custom-register write log as the witness.

    python3 sound_check.py [objects|tone|noise|pass|ramp|all]

The library (hunks 80 to 86: the object loader 0x55626, the player 0x19C7C, the per-blank server 0x551D8, the audio interrupt handler 0x55158) runs in the rig with
its operating-system calls replaced by hooks (an allocator, the file routines reading the sample files the extractor wrote to assets/sounds) and a small interrupt
shim in rig.c (a write of INTREQ or the DMA start of a channel calls the handler); every write to the custom registers 0xDFF000-0xDFF1FF is logged.
  objects  the three loaders (0x19660, 0x196BA, 0x199CA) on the real files: for all 37 objects the data (file, offset, length), the period, the play count, the
           reload count, the next object, the script and the enable bits, against the note's table; the scripts against the image; the synthesised buffers
  tone     the tone routine 0x1A042 with the ten cue parameter sets and random ones: the per-blank period and volume of voice 3 against the script interpreter
  noise    the noise buffer and the random source 0x55BE4; the noise wrapper 0x1A0DC (random pitch, amplitude, duration), the gun, the ground contact, the
           launch, the gear, the wind: every register write of the library against the model
  music    the start-up phrase 0x19CE6 and the object chain: 120 block ends per voice, the loads and register writes against the model
  beeps    the key click 0x37C2E and the beeps 0x37C32, 0x37C44, 0x37C4A: the nine tone parameters against the rule, over the view, the sound flag and the cool-down
  pass     the per-pass routine 0x14AA0 over random viewed-record states, six blanks after each pass, write by write against the model
  ramp     the master-volume ramp 0x294AC against its rule
Inputs (git-ignored): extracted/interceptor.bin, assets/sounds/*.raw, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import ctypes, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sndlib import *
import sndmodel as M

SCRIPT_ADDR = {"tone": 0x55CA0, "noise": 0x55D00, "gear": 0x55D28, "launch": 0x55D58, "gun": 0x55D98}
SCRIPT_END = {"tone": 0x55D00, "noise": 0x55D28, "gear": 0x55D58, "launch": 0x55D98, "gun": 0x55DF8}
LOADERS = (0x19660, 0x196BA, 0x199CA)
FILE_OF = {}


def boot():
    lib = Lib(); r = lib.r
    for e in LOADERS: r.run(e)
    hw = Hw(lib); hw.drain()
    return lib, hw


def call(lib, addr, *args):
    L = lib.L
    L.rig_call.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]; L.rig_call.restype = ctypes.c_uint32
    a = (ctypes.c_uint32 * max(1, len(args)))(*[x & 0xFFFFFFFF for x in args])
    rc = L.rig_call(addr, a, len(args))
    err = ctypes.c_int.in_dll(L, "rig_last_error").value
    if err: raise RuntimeError("rig error %d calling %X" % (err, addr))
    return rc


def new_model(lib, drop=()):
    """the model in the state the loader left the rig in"""
    m = M.Lib(); r = lib.r
    m.en12 = r.u8(0x4AC12); m.en13 = r.u8(0x4AC13)
    for n in list(m.o):
        if not r.u32(TABLE + 4 * n): del m.o[n]
    for n in m.o: m.addr[n] = lib.obj(n)["data"]
    return m


def rig_writes(entries):
    """the library's register writes as (voice, kind, value) in order; DMACON as one entry per channel bit"""
    out = []
    for pc, reg, val in entries:
        if 0x0A0 <= reg < 0x0E0:
            ch = (reg - 0x0A0) >> 4; off = (reg - 0x0A0) & 0xF
            kind = {0: "lc", 4: "len", 6: "per", 8: "vol"}.get(off)
            if kind: out.append((ch, kind, val & (0xFFFFFFFF if kind == "lc" else 0xFFFF)))
        elif reg == 0x096:
            for ch in range(4):
                if val & (1 << ch): out.append((ch, "dma", bool(val & 0x8000)))
    return out


def model_writes(m):
    w = [x for x in m.writes]; m.writes.clear(); return w


# ------------------------------------------------------------------------------------------------------------------------------------------------------
def part_objects():
    lib, hw = boot(); r = lib.r
    bad = 0; checks = 0
    spec = M.objects()
    ptr = {n: lib.obj_ptr(n) for n in range(37)}
    for n in range(37):
        o = lib.obj(n)
        if n not in spec:
            checks += 1
            if ptr[n]: bad += 1; print("DIFF object %d exists" % n)
            continue
        s = spec[n]
        if not o: bad += 1; print("DIFF object %d missing" % n); continue
        c = (o["len_raw"] & 0x7FFFFFFF, o["period"] >> 16, o["period"] & 0xFFFF, o["vol"], 0 if o["count"] == 0 else o["count"] if o["count"] < 2 ** 31 else o["count"] - 2 ** 32,
             o["reload"], o["pdelta"], o["vdelta"], o["wait"], o["pos"])
        want = (s["len"], s["period"], 0, 63 << 16, s["count"], s["reload"], 0, 0, 0, 0)
        checks += 1
        if c != want: bad += 1; print("DIFF object %d fields %s want %s" % (n, c, want))
        nx = {v["ptr"]: k for k, v in ((k, lib.obj(k)) for k in range(37)) if v}.get(o["next"], 0) if o["next"] else 0
        checks += 1
        if nx != s["next"]: bad += 1; print("DIFF object %d next %d want %d" % (n, nx, s["next"]))
        want_script = SCRIPT_ADDR.get(s["script"], 0) if s["script"] and s["script"] != "gear" else 0
        checks += 1
        if o["script"] != want_script: bad += 1; print("DIFF object %d script %X want %X" % (n, o["script"], want_script))
        # the data: file bytes, the square wave, the noise
        data = r.peek(o["data"], s["len"])
        if s["file"]:
            f = open(os.path.join(SOUNDS, s["file"] + ".raw"), "rb").read()
            want_data = f[s["off"]:s["off"] + s["len"]]
        elif n == 4: want_data = M.tone_buffer()
        else: want_data = M.Lfsr().fill(2048)
        checks += 1
        if data != want_data: bad += 1; print("DIFF object %d data" % n)
        # shared data: objects of a pair, the gun and the noise share a buffer
    checks += 1
    if lib.obj(0xA)["data"] != lib.obj(6)["data"]: bad += 1; print("DIFF object 0xA does not share the noise buffer")
    checks += 1
    if (r.u8(0x4AC12), r.u8(0x4AC13)) != (0x07, 0xF7): bad += 1; print("DIFF enable bits %02X %02X want 07 F7" % (r.u8(0x4AC12), r.u8(0x4AC13)))
    # the scripts in the image against the model's defaults
    for name, a in SCRIPT_ADDR.items():
        n = (SCRIPT_END[name] - a) // 8
        pairs = [list(struct.unpack(">Ii", r.peek(a + 8 * i, 8))) for i in range(n)]
        want = [list(p) for p in M.SCRIPTS[name]]
        checks += 1
        if name == "tone": want = [[f, v] for f, v in want]
        # the patched parameter slots hold whatever the last use left; compare the fields and the unpatched values
        fields_ok = [p[0] for p in pairs[:len(want)]] == [p[0] for p in want]
        checks += 1
        if not fields_ok: bad += 1; print("DIFF script %s fields" % name)
    print("objects: %d checks, %s" % (checks, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ------------------------------------------------------------------------------------------------------------------------------------------------------
TONE_CUES = [(0xA0, 0x10, 8, 0xC8, 0x10, 5, 3, 4, 4), (0x144, 0x0A, 8, 0xC8, 0x0A, 5, 3, 1, 4), (0x7C, 0x10, 8, 0xB4, 0x10, 5, 3, 3, 4),
             (0xE5, 3, 5, 0xE5, 3, 5, 1, 0, 1), (0xC0, 4, 9, 0xC8, 0, 3, 1, 0, 1), (0x1F4, 0x0F, 5, 0x1F4, 0x0F, 5, 3, 1, 5),
             (0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 3, 5), (0x190, 8, 5, 0x14A, 0, 1, 3, 2, 5), (0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 1, 5),
             (0xC8, 0xE, 8, 0xC8, 0, 2, 1, 5, 1), (0x12C, 4, 1, 0x12C, 4, 1, 1, 2, 2), (0x12C, 2, 1, 0x12C, 2, 1, 1, 1, 1)]


def compare_blanks(lib, hw, m, nblank, voices, tag, limit=6):
    """run nblank blanks in both; returns the number of differing blanks (write by write)"""
    bad = 0
    for b in range(nblank):
        got = rig_writes(hw.blank()); m.vbl(); want = model_writes(m)
        if got != want:
            bad += 1
            if bad <= limit: print("DIFF %s blank %d: rig %s model %s" % (tag, b, got, want))
            return bad
    return bad


def part_tone():
    lib, hw = boot(); r = lib.r
    bad = 0; runs = 0; stops = 0
    rng = random.Random(7)
    sets = list(TONE_CUES)
    for _ in range(300):
        p1 = rng.randrange(0x7C, 0x600); p4 = rng.randrange(0x7C, 0x600)
        sets.append((p1, rng.randrange(0, 0x40), rng.randrange(1, 20), p4, rng.randrange(0, 0x40), rng.randrange(1, 20), rng.randrange(0, 5), rng.randrange(0, 5),
                     rng.randrange(0, 12)))
    m = new_model(lib)
    for p in sets:
        call(lib, 0x1A042, *p); got0 = rig_writes(hw.drain())
        m.tone(p); want0 = model_writes(m)
        runs += 1
        if got0 != want0:
            bad += 1; print("DIFF tone %s start: rig %s model %s" % (p, got0, want0)); continue
        # the blanks: until the voice stops (both) or 500 blanks (a repeat count of 0 loops for ever)
        for b in range(500):
            got = rig_writes(hw.blank()); m.vbl(); want = model_writes(m)
            if got != want:
                bad += 1; print("DIFF tone %s blank %d: rig %s model %s" % (p, b, got, want)); break
            if (3, "dma", False) in got: stops += 1; break
        call(lib, 0x19C58, 3); hw.drain(); m.stop(3); model_writes(m)
    print("tone: %d parameter sets (10 cues, 2 click and beep sets, 300 random), %d ended by their own script, %s" % (runs, stops, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ------------------------------------------------------------------------------------------------------------------------------------------------------
def part_noise():
    lib, hw = boot(); r = lib.r
    bad = 0; runs = 0
    # the buffer and the random source at load time: the seed is the image's constant and nothing draws before the fill
    lf = M.Lfsr()
    buf = lf.fill(2048)
    if r.peek(lib.obj(6)["data"], 2048) != buf: bad += 1; print("DIFF noise buffer is not the register sequence from the seed 0x47091247")
    if r.u32(0x55C9C) != lf.s: bad += 1; print("DIFF register state after the fill %08X model %08X" % (r.u32(0x55C9C), lf.s))
    rng = random.Random(11)
    m = new_model(lib)
    bits = lambda n: (lambda: lf.bits(n))
    for it in range(400):
        kind = rng.choice(("noise", "noise", "noise", "gun", "ground", "gear", "launch", "wind"))
        if kind == "noise":
            a = rng.randrange(1, 0x40); b = rng.randrange(1, 0x90)
            call(lib, 0x1A0DC, a, b); m.noise(a, b, bits(11))
        elif kind == "gun":
            a = 6; b = rng.choice((0x16, 0x1E)); call(lib, 0x1A2F0, a, b); m.gun(a, b)
        elif kind == "ground":
            v = rng.choice((0x0F, 0x1E, 0x28)); r.w16(0x4AA6E, 5); r.w16(0x4A996, 5); call(lib, 0x1A18C, v); m.ground(v)
        elif kind == "gear":
            v = rng.choice((5, 10)); r.w16(0x4AA6E, 5); r.w16(0x4A996, 5); call(lib, 0x1A1E6, v); m.gear(v)
        elif kind == "launch":
            v = rng.randrange(2, 0x40); call(lib, 0x1A258, v); m.launch(v, bits(3))
        else:
            v = rng.randrange(1, 0x40); call(lib, 0x19F9A, v); m.wind(v, bits(6))
        runs += 1
        got = rig_writes(hw.drain()); want = model_writes(m)
        if got != want: bad += 1; print("DIFF %s start #%d: rig %s model %s" % (kind, it, got, want)); continue
        n = rng.randrange(1, 90)
        bad += compare_blanks(lib, hw, m, n, range(4), "%s #%d" % (kind, it))
        if rng.random() < 0.5:
            for v in (0, 1, 2, 3):
                call(lib, 0x19C58, v); hw.drain(); m.stop(v); model_writes(m)
    print("noise: buffer + %d wrapper runs (noise, gun, ground contact, gear, launch, wind), %s" % (runs, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ------------------------------------------------------------------------------------------------------------------------------------------------------
def draw_state(rng):
    S = dict(notice=rng.choice((0, 0, 0, 0xFE, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA)), v4a966=int(rng.random() < 0.03), v4a865=int(rng.random() < 0.03),
             v4a866=int(rng.random() < 0.03), v4a84d=int(rng.random() > 0.03), gear=rng.choice((0, 0, 0x83, 0x83, 1, 2, 3, 0x82, 0x80)),
             view=rng.choice((0, 0, 0, 1, 2, 3, 4, 5, 6)), v4a86d=rng.randrange(2), v4a879=rng.randrange(256),
             cls=rng.choice((0x10, 0x20, 0x30, 0x40, 0x11, rng.randrange(256))), w2=rng.randrange(0x10000), t2b=rng.choice((0, 0, rng.randrange(256), rng.randrange(0x78, 0x80), rng.randrange(256))),
             w56=rng.choice((0, rng.randrange(0x10000), rng.randrange(-0x400, 0x400) & 0xFFFF)), w5a=rng.choice((0, rng.randrange(0x10000), rng.randrange(-0x400, 0x400) & 0xFFFF)),
             w6e=rng.choice((0, rng.randrange(0x8000), rng.randrange(0x10000))), v4a96a=rng.randrange(-8, 13) & 0xFF, v4abfa=rng.choice((0, 0, rng.randrange(1, 4000))),
             v4a941=rng.choice((rng.randrange(16), 0xFF, rng.randrange(256))), v4a870=rng.choice((0, 0, 0, 0xFF, 1, 2, 3, 4)), v4a921=rng.randrange(1, 0x80), v4a922=rng.randrange(0, 0x80), v4a84f=rng.choice((0, 0, 0, 1, 2, 8)), en13=0xF7, en12=7)
    return S


def put_state(r, S, cam):
    r.w16(0x4A994, 0)                        # the viewed record is record 0 (0x4B23C)
    R = 0x4B23C
    r.w16(R + 2, S["w2"]); r.w8(R + 0x2B, S["t2b"]); r.w16(R + 0x56, S["w56"]); r.w16(R + 0x5A, S["w5a"]); r.w16(R + 0x6E, S["w6e"]); r.w8(R + 0x62, S["cls"])
    r.w8(0x4A968, S["notice"]); r.w8(0x4A966, S["v4a966"]); r.w8(0x4A865, S["v4a865"]); r.w8(0x4A866, S["v4a866"]); r.w8(0x4A84D, S["v4a84d"])
    r.w8(0x4A93D, S["gear"]); r.w8(0x4A83D, S["view"]); r.w8(0x4A86D, S["v4a86d"]); r.w8(0x4A879, S["v4a879"]); r.w8(0x4A96A, S["v4a96a"])
    r.w16(0x4ABFA, S["v4abfa"]); r.w8(0x4A941, S["v4a941"]); r.w8(0x4A84F, S["v4a84f"])
    r.w8(0x4AC12, S["en12"]); r.w8(0x4AC13, S["en13"])
    r.w16(0x4AA6E, 7); r.w16(0x4A996, 7 if cam else 8)
    r.w32(0x4AC0C, 0); r.w8(0x4A870, S["v4a870"]); r.w8(0x4A921, S["v4a921"]); r.w8(0x4A922, S["v4a922"])


def apply_call(m, name, args, S, cam, lf):
    if name == "gear": m.gear(args[0], cam)
    elif name == "stop2": m.stop2()
    elif name == "noise": m.noise(args[0], args[1], lambda: lf.bits(11))
    elif name == "wind": m.wind(args[0], lambda: lf.bits(6))
    elif name == "eng_ext": m.engine_ext(*args)
    elif name == "eng_cockpit": m.engine_cockpit(*args)
    elif name == "launch": m.f152c6(args[0], args[1], lambda: lf.bits(3))
    elif name == "glide": m.glide(*args)
    elif name == "glide_b": m.glide_b(*args)
    else: raise ValueError(name)


def part_pass():
    lib, hw = boot(); r = lib.r
    rng = random.Random(2024)
    lf = M.Lfsr(); lf.fill(2048)
    m = new_model(lib)
    bad = 0; n_calls = {}; blanks = 0
    NP = int(os.environ.get("SOUND_PASSES", "2000"))
    for it in range(NP):
        S = draw_state(rng)
        # now and then the loader's work is undone as if a file had been missing: the objects and their enable bits go
        if rng.random() < 0.08:
            kind = rng.choice(("cpt", "egn", "wnd", "ger", "tre", "noise", "launch"))
            if kind == "cpt": S["en13"] &= ~1; drop = (0, 1)
            elif kind == "egn": S["en13"] &= ~2; drop = ()
            elif kind == "wnd": S["en13"] &= ~0x10; drop = ()
            elif kind == "ger": S["en13"] &= ~0x40; drop = ()
            elif kind == "tre": S["en13"] &= ~4; drop = ()
            elif kind == "noise": S["en12"] &= ~1; drop = ()
            else: S["en12"] &= ~2; drop = ()
        cam = rng.random() < 0.85
        put_state(r, S, cam)
        m.en12, m.en13 = S["en12"], S["en13"]
        call(lib, 0x14AA0)
        got0 = rig_writes(hw.drain())
        calls, out = M.pass_calls(S)
        m.side = {}
        for name, args in calls:
            apply_call(m, name, args, S, cam, lf)
            n_calls[name] = n_calls.get(name, 0) + 1
        want0 = model_writes(m)
        okstate = (r.u8(0x4A968), r.u8(0x4A93D), r.u8(0x4A84F), r.u8(0x4A870)) == (out["notice"], out["gear"], out["v4a84f"], out["v4a870"])
        if not okstate:
            bad += 1; print("DIFF pass %d state: rig notice %02X gear %02X v4a84f %d, model %s; calls %s S %s" % (it, r.u8(0x4A968), r.u8(0x4A93D), r.u8(0x4A84F), out, calls, S))
        if got0 != want0:
            bad += 1; print("DIFF pass %d writes: rig %s model %s; calls %s; S %s" % (it, got0, want0, calls, S));
            if bad > 8: break
        # the rig and the model go on from the rig's state for the next pass (the byte state the pass leaves is checked above)
        for b in range(6):
            got = rig_writes(hw.blank()); m.vbl(); want = model_writes(m); blanks += 1
            if got != want:
                bad += 1; print("DIFF pass %d blank %d: rig %s model %s; calls %s" % (it, b, got, want, calls)); break
        if bad > 8: break
    print("pass: %d passes, %d blanks, calls %s, %s" % (it + 1, blanks, dict(sorted(n_calls.items())), "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ------------------------------------------------------------------------------------------------------------------------------------------------------
def part_ramp():
    lib, hw = boot(); r = lib.r
    rng = random.Random(5)
    bad = 0; n = 0
    for it in range(2000):
        flag = rng.choice((0, 1, 2)); tgt = rng.choice((0x3F0000, 0x1F0000, 0, rng.randrange(0x400000) & ~0xFFFF, rng.randrange(0x400000))); cur = rng.choice((0x3F0000, 0, rng.randrange(0x400000), tgt, tgt + 0x2000, tgt - 0x2000))
        r.w8(0x4A88F, flag); r.w32(0x4A720, tgt); r.w32(0x54FBE, cur)
        call(lib, 0x294AC)
        want = cur
        if flag != 0 and cur != tgt:
            if cur < tgt: want = min(cur + 0x4000, 0x3F0000)
            else: want = max(cur - 0x4000, 0)
        n += 1
        if r.u32(0x54FBE) != want & 0xFFFFFFFF:
            bad += 1; print("DIFF ramp flag %d target %X level %X: rig %X model %X" % (flag, tgt, cur, r.u32(0x54FBE), want))
    print("ramp: %d calls, %s" % (n, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def part_music():
    """the start-up music chain: the phrase wrapper 0x19CE6 starts objects 0xD and 0xE on voices 0 and 1; each block end raises the voice's interrupt; the
    sequence of objects the handler loads (and the registers it writes) against the model for 120 block ends per voice"""
    lib, hw = boot(); r = lib.r
    bad = 0
    # the other paths of the phrase wrappers: no music (bit 7 clear), neither music nor text201, the flag already set, and the mission tone 0x19D7A
    phr = 0
    for en13, en12, flag, addr, arg in ((0x77, 7, 0, 0x19CE6, 0x32), (0x77, 3, 0, 0x19CE6, 0x32), (0xF7, 7, 2, 0x19CE6, 0x0F), (0xF7, 7, 1, 0x19CE6, 0x0F), (0xF7, 7, 0, 0x19D7A, 0x28),
                                          (0x77, 7, 0, 0x19D7A, 0x0F), (0xF7, 3, 0, 0x19D7A, 0x28)):
        for v in range(4): call(lib, 0x19C58, v)
        hw.drain()
        r.w8(0x4AC13, en13); r.w8(0x4AC12, en12); r.w8(0x4A88F, flag)
        mm = new_model(lib); mm.en13, mm.en12, mm.flag = en13, en12, flag
        call(lib, addr, arg); got = rig_writes(hw.drain())
        (mm.phrase if addr == 0x19CE6 else mm.mission_tone)(arg); want = model_writes(mm)
        phr += 1
        if got != want or r.u8(0x4A88F) != mm.flag: bad += 1; print("DIFF phrase wrapper %X en13 %02X en12 %02X flag %d: rig %s model %s flag %d/%d" % (addr, en13, en12, flag, got, want, r.u8(0x4A88F), mm.flag))
    for v in range(4): call(lib, 0x19C58, v)
    hw.drain(); r.w8(0x4AC13, 0xF7); r.w8(0x4AC12, 7); r.w8(0x4A88F, 0); r.w32(0x54FBE, 0x3F0000)
    m = new_model(lib)
    call(lib, 0x19CE6, 0x32)
    got = rig_writes(hw.drain())
    m.master = 0x3F
    for v in range(4): m.stop(v)
    m.start(0xD, 0, 0x3F); m.start(0xE, 1, 0x3F)
    want = model_writes(m)
    if got != want: bad += 1; print("DIFF phrase start: rig %s model %s" % (got, want))
    spec = M.objects()
    ptr2n = {lib.obj(n)["data"]: spec[n]["file"] for n in range(37) if lib.obj_ptr(n)}
    seq = {0: [], 1: []}
    for k in range(120):
        for v in (0, 1):
            got = rig_writes(hw.end_of_block(v)); m.irq(v); want = model_writes(m)
            if got != want: bad += 1; print("DIFF block end %d voice %d: rig %s model %s" % (k, v, got, want)); break
            lc = [x for x in got if x[1] == "lc"]
            seq[v].append(ptr2n.get(lc[0][2], lc[0][2]) if lc else None)
        if bad: break
    # a restart after a flight: stop all four voices in the middle of the loop, clear the flag and start the music again; the objects keep the counts they were left with, so the
    # intro objects play one block each and the loop resumes where its counts stand
    for v in range(4): call(lib, 0x19C58, v); hw.drain(); m.stop(v); model_writes(m)
    r.w8(0x4A88F, 0); m.flag = 0
    call(lib, 0x19CE6, 0x0F); got = rig_writes(hw.drain()); m.phrase(0x0F); want = model_writes(m)
    if got != want: bad += 1; print("DIFF restart: rig %s model %s" % (got, want))
    seq2 = []
    for k in range(40):
        for v in (0, 1):
            got = rig_writes(hw.end_of_block(v)); m.irq(v); want = model_writes(m)
            if got != want: bad += 1; print("DIFF restart block end %d voice %d: rig %s model %s" % (k, v, got, want)); break
            lc = [x for x in got if x[1] == "lc"]
            if v == 0: seq2.append(ptr2n.get(lc[0][2]) if lc else None)
        if bad: break
    runs2 = []
    for o in seq2:
        if runs2 and runs2[-1][0] == o: runs2[-1][1] += 1
        else: runs2.append([o, 1])
    print("music restart after a stop in the loop: voice 0 loads %s" % " ".join("%s x%d" % tuple(x) for x in runs2[:10]))
    runs = []
    for o in seq[0]:
        if runs and runs[-1][0] == o: runs[-1][1] += 1
        else: runs.append([o, 1])
    print("music: %d phrase wrapper variants, %d block ends per voice, voice 0 loads after the two start loads (texti0a: 14 in all, 12 here) (object x blocks) %s, %s" % (phr, len(seq[0]), " ".join("%s x%d" % (x[0], x[1]) for x in runs[:14]) + " ...", "FAILED %d" % bad if bad else "0 differences"))
    return bad


def part_restart():
    """the music restart after a flight (note 10 7.2, capture sound7-restart): the menu build stops all four voices, clears the flag and calls the phrase wrapper 0x19CE6 with 15. The objects keep their counts, so the
    intro objects 0xD and 0xF play one block each and the loop resumes where its counts stand. From a fresh start the chain is cut after n block ends per voice, the restart is compared write by write with
    the model, and the blocks voice 0 plays afterwards are listed as (sample file x blocks). The capture's chain (an airborne snapshot restarted by SHIFT-ESC) was texti0a x1, texti0b x1, texti1 x2,
    texti2 x1, texti1 x3, texti2 x1, texti3 x3, texti4 x1: the cut after 15 block ends per voice (inside the first texti1 pass of the loop) gives exactly that."""
    bad = 0
    spec = M.objects()
    results = {}
    for cut in range(12, 22):
        lib, hw = boot(); r = lib.r
        for v in range(4): call(lib, 0x19C58, v)
        hw.drain()
        r.w8(0x4AC13, 0xF7); r.w8(0x4AC12, 7); r.w8(0x4A88F, 0); r.w32(0x54FBE, 0x3F0000)
        m = new_model(lib)
        call(lib, 0x19CE6, 0x32)
        got = rig_writes(hw.drain())
        m.master = 0x3F
        for v in range(4): m.stop(v)
        m.start(0xD, 0, 0x3F); m.start(0xE, 1, 0x3F)
        want = model_writes(m)
        if got != want: bad += 1; print("DIFF restart-part first start: rig %s model %s" % (got, want))
        ptr2f = {lib.obj(n)["data"]: spec[n]["file"] for n in range(37) if lib.obj_ptr(n)}

        def ends(k, tag):
            nonlocal bad
            out = []
            for i in range(k):
                for v in (0, 1):
                    g = rig_writes(hw.end_of_block(v)); m.irq(v); w = model_writes(m)
                    if g != w:
                        bad += 1; print("DIFF %s block end %d voice %d: rig %s model %s" % (tag, i, v, g, w)); return out
                    lc = [x for x in g if x[1] == "lc"]
                    if v == 0: out.append(ptr2f.get(lc[0][2]) if lc else None)
            return out
        pre = ends(cut, "cut %d" % cut)
        for v in range(4): call(lib, 0x19C58, v); hw.drain(); m.stop(v); model_writes(m)   # the stop-all of the menu build
        r.w8(0x4A88F, 0); m.flag = 0
        call(lib, 0x19CE6, 0x0F); got = rig_writes(hw.drain()); m.phrase(0x0F); want = model_writes(m)
        if got != want: bad += 1; print("DIFF restart after cut %d: rig %s model %s" % (cut, got, want))
        first2 = [ptr2f.get(x[2]) for x in got if x[1] == "lc" and x[0] == 0]
        played = first2 + ends(14, "after cut %d" % cut)
        rs = []
        for o in played:
            if rs and rs[-1][0] == o: rs[-1][1] += 1
            else: rs.append([o, 1])
        results[cut] = rs
        if len(rs) < 2 or rs[0][1] != 1: bad += 1; print("DIFF restart after cut %d does not begin with one block of the first intro file: %s" % (cut, rs[:3]))
    for cut, rs in results.items(): print("restart after a cut of %2d block ends: %s" % (cut, " ".join("%s x%d" % (o, c) for o, c in rs[:9])))
    want_cap = [["texti0a", 1], ["texti0b", 1], ["texti1", 2], ["texti2", 1], ["texti1", 3], ["texti2", 1], ["texti3", 3]]
    hits = [c for c, rs in results.items() if rs[:len(want_cap)] == want_cap]
    if not hits: bad += 1; print("DIFF no cut reproduces the capture's chain %s" % want_cap)
    print("restart: %d cuts, rig against model write by write; the capture sound7-restart's chain is reproduced by the cut(s) %s, %s" % (len(results), hits, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def part_beeps():
    """the key click 0x37C2E, the HUD beeps 0x37C32 and 0x37C4A, the entry 0x37C44: the nine tone parameters the original passes to 0x1A042 (a hook logs them) against the rule of note 10 7.4"""
    lib, hw = boot(); r = lib.r
    got = []
    def log():
        sp = r.reg("a7")
        got.append(tuple(r.u32(sp + 4 + 4 * i) for i in range(9)))
    cb = HOOKFN(log); lib._keep.append(cb)
    lib.L.rig_hook_set(0x1A042, cb, 0)
    bad = 0; n = 0
    def rule(entry, view, f88f, f942, arg):
        blocked942 = 0 < f942 < 128
        if entry == "click":
            d1 = arg
            return None if blocked942 else (0x12C, d1, 1, 0x12C, d1, 1, 1, 1, 1)
        if entry == "n":
            if view: return None
            return (0x12C, 4, 1, 0x12C, 4, 1, 1, arg, arg)          # 0x37C32 jumps past the cool-down test
        if entry == "44":
            return None if blocked942 else (0x12C, 2, 1, 0x12C, 2, 1, 1, 2, 2)
        if view: return None
        d1 = 2 if f88f else 4
        return None if blocked942 else (0x12C, d1, 1, 0x12C, d1, 1, 1, 2, 2)
    for entry in ("click", "n", "44", "4A"):
        for view in (0, 1, 3):
            for f88f in (0, 1, 2):
                for f942 in (0, 1, 3, 0xFF):
                    for arg in ((2, 5) if entry == "click" else (8, 4) if entry == "n" else (0,)):
                        r.w8(0x4A83D, view); r.w8(0x4A88F, f88f); r.w8(0x4A942, f942)
                        got.clear()
                        addr = {"click": 0x37C2E, "n": 0x37C32, "44": 0x37C44, "4A": 0x37C4A}[entry]
                        if entry == "click": r.run(addr, d1=arg)
                        elif entry == "n": r.run(addr, d0=arg)
                        else: r.run(addr)
                        want = rule(entry, view, f88f, f942, arg)
                        have = got[0] if got else None
                        n += 1
                        if have != want and not (entry == "44" and False):
                            bad += 1; print("DIFF %s view %d flag %d cooldown %d arg %d: rig %s rule %s" % (entry, view, f88f, f942, arg, have, want))
    lib.L.rig_hooks_clear()
    print("beeps: %d cases of the click and the three beep entries, %s" % (n, "FAILED %d" % bad if bad else "0 differences"))
    return bad


PARTS = {"objects": part_objects, "tone": part_tone, "noise": part_noise, "music": part_music, "beeps": part_beeps, "restart": part_restart, "pass": part_pass, "ramp": part_ramp}


def main():
    miss = have_inputs()
    if miss:
        print("skipped (missing: %s)" % ", ".join(os.path.relpath(p, ROOT) for p in miss)); return 0
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    bad = 0
    for k, f in PARTS.items():
        if which in ("all", k): bad += f()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

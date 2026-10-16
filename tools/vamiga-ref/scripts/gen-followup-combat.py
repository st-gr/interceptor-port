#!/usr/bin/env python3
"""Generates the capture scripts of the milestone-6 sweep of note 07 (followup-combat-*.txt, one run each; the scripts and the logs of the others are plain text).

    python3 gen-followup-combat.py eject-cockpit   > followup-combat-eject-frames.txt        SHIFT-E in the cockpit view, every video frame, frames saved
    python3 gen-followup-combat.py eject-external  > followup-combat-eject-frames-ext.txt    the same from the external view (the seat's level 2 to level 1)
    python3 gen-followup-combat.py gun             > followup-combat-kills.txt               a fresh MiG in formation 12 passes ahead, designated, the gun held
    python3 gen-followup-combat.py missile         > followup-combat-kills-missile.txt       the same 40 passes ahead, the Sidewinder fired at it
    python3 gen-followup-combat.py flare|chaff|none > followup-combat-flare-chaff-<x>.txt    external view, a flare or a chaff requested by the poke of the request byte, frames per video frame
    python3 gen-followup-combat.py wrap            > followup-combat-effect-wrap.txt         an effect object with its life word poked to 0x7FFF and other values past 0x80
    python3 gen-followup-combat.py rangekey        > followup-combat-range-key.txt           the range key R pressed many times; the nibble +0x63 read per press; the scope frame
    python3 gen-followup-combat.py scope           > followup-combat-scope.txt               a target at known ranges ahead, the scope frame per nibble
    python3 gen-followup-combat.py firstpass       > followup-combat-first-pass.txt          free flight from the aircraft screen: V:4A8EE and V:4A8EF per frame
    python3 gen-followup-combat.py crashnibble     > followup-combat-crash-nibble.txt        a dive with the ejection key pressed before the crash (the hand-over with a nibble)
    python3 gen-followup-combat.py seatshot        > followup-combat-seat-shot.txt           the seat marked crashed by a poke in flight (the dying branch of the seat's step)
    python3 gen-followup-combat.py gunkillpost[-ext] > followup-combat-gunkill-post[-ext].txt  the gun kill of a designated MiG per video frame (V:4AB98: the 0x4016 post), cockpit and external view
    python3 gen-followup-combat.py parachute       > followup-combat-parachute.txt            the low nibble of the player's gear byte set to 15: the class 0x31 record (the parachute) in flight, per 12 frames
    python3 gen-followup-combat.py launchorder     > followup-combat-launch-order.txt        both launch requests pending in one pass (poked: no code sets them)
    python3 gen-followup-combat.py threat          (post-processes gen-combat-cm.py missile: V:4A925 per sample)

Run from the repository root under ulimit -v 2500000 (gun, missile, scope and launchorder use the Musashi rig to get the fields of the mode-3 placement of a hostile
record, and the log of combat-dying-probe.txt for the airborne snapshot's player record).  A staged record is the placement's first 0xA4 bytes (fields, never the
model vertices behind them); its template descriptor is moved by the `copy` command inside the machine; no byte of the game is in a script.
"""
import sys, struct, os, subprocess
sys.path.insert(0, 'tools/vamiga-ref'); sys.path.insert(0, 'tools/m68k-rig/tests/lists_model')
kind = sys.argv[1] if len(sys.argv) > 1 else 'gun'
R = lambda i: 0x4B23C + 0x200 * i
LOGREC = 'C54498'
P0 = 0xE7                      # V:4A992 at the start of airborne.vasnap (combat-hit-gun.txt s0_4A992)
SEG = 'seglist extracted/interceptor.bin extracted/interceptor.map.json'
out = []


def head(snap='airborne'):
    out.extend(['restore extracted/captures/%s.vasnap' % snap, SEG, 'freeze'])


def pokerec(i, rec):
    for o in range(0, 0xA4, 4):
        out.append('poke F:%X 4 0x%08X' % (R(i) + o, struct.unpack_from('>I', rec, o)[0]))


def player():
    import logparse
    L = logparse.load('extracted/captures/logs/combat-dying-probe.txt')
    return bytes(L['rec0'].data)


def placed_mig(slot=8, tpl=6):
    from combat import stage, REC
    r = stage(mode=3)
    return bytearray(r.peek(REC(slot), 0xA4))


def formation(p0, N, slot=8, tpl=6, throttle=90, group=3, w0=0x1140, w2=0x0100):
    """a hostile that flies the player's attitude and speed (the throttle byte 90 keeps the speed: rig measurement of the staging), N passes of the player's velocity ahead,
    no AI (W0 bit 0x0080 clear, W2 bit 0x0100: scripted without a script), no destination, a fresh hit counter"""
    rec = placed_mig(slot)
    def w16(o, v): rec[o:o + 2] = struct.pack('>H', v & 0xFFFF)
    def w32(o, v): rec[o:o + 4] = struct.pack('>I', v & 0xFFFFFFFF)
    rec[0x80:0x92] = p0[0x80:0x92]; rec[0x92:0xA4] = p0[0x92:0xA4]; rec[0x66:0x6C] = p0[0x66:0x6C]
    pos = [struct.unpack('>i', p0[o:o + 4])[0] for o in (0x14, 0x18, 0x1C)]
    vel = [struct.unpack('>i', p0[o:o + 4])[0] for o in (0x3E, 0x42, 0x46)]
    tp = [pos[k] + N * vel[k] for k in range(3)]
    w16(0, w0); w16(2, w2); rec[5] = 0; rec[0x20] = 0; rec[0x38] = 0xFF; rec[0x5E] = group; rec[0x3C] = 0
    w16(0x2C, 0xFFFF); w16(0x4C, 40); rec[0x2B] = throttle
    rec[0x6C:0x70] = p0[0x6C:0x70]
    for o, v in zip((0x3E, 0x42, 0x46), vel): w32(o, v)
    w32(0x14, tp[0]); w32(0x18, tp[1]); w32(0x1C, tp[2])
    w16(6, (tp[0] & 0xFFFFFFFF) >> 22); w16(8, (tp[2] & 0xFFFFFFFF) >> 22)
    w16(0xC, (tp[0] & 0x3FFFFF) >> 8); w16(0xE, (tp[2] & 0x3FFFFF) >> 8); w32(0x10, tp[1] >> 8)
    out.append('copy F:%X F:%X 20' % (0x25ECC + 20 * tpl, 0x2600C + 20 * slot))
    pokerec(slot, rec)
    return rec


def samples_kills(n, step, fire):
    V1 = ['4A963', '4A961', '4A910', '4A872', '4A87D', '4A920', '4A96C']
    V2 = ['4A992', '4AA78', '4A97E', '54E72', '4AB98', '4A984']
    out.append('peek F:1D024 4 recptr')
    for k in range(n):
        if k < 4: out.append('poke F:4AA78 2 0x1000')
        fire(k)
        out.append('peek F:%X 128 s%d_r0' % (R(0), k))
        out.append('peek F:%X 128 s%d_r8' % (R(8), k))
        out.append('peek F:%X 128 s%d_r1' % (R(1), k))
        out.append('peek F:4AD2A 640 s%d_pool' % k)
        out.append('peek R:%s 80 s%d_log' % (LOGREC, k))
        for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
        for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
        out.append('step %d' % step_)


if kind in ('gun', 'missile'):
    p0 = player()
    head()
    formation(p0, 12 if kind == 'gun' else 40)
    sel = (0x10 if kind == 'gun' else 0x20) | (p0[0x63] & 0x0F)
    out.append('poke F:%X 1 0x%02X' % (R(0) + 0x63, sel))
    out.append('poke F:%X 1 0x44' % (R(0) + 0x5F))
    step_ = 6
    def fire(k):
        if kind == 'gun' and k == 3: out.append('keydown SPACE')
        if kind == 'missile' and k == 3: out.append('key SPACE 0.12')
    samples_kills(170 if kind == 'gun' else 200, step_, fire)
    if kind == 'gun': out.append('keyup SPACE')

elif kind in ('flare', 'chaff', 'none', 'chaffback', 'flareback', 'chaffhead'):
    head()
    out += (['key KPENTER 0.12', 'step 20', 'key KP8 0.12', 'step 20'] if kind == 'chaffhead' else ['key KPENTER 0.12' if not kind.endswith('back') else 'key KP2 0.12']) + [ 'stepuntil F:4A992 2 eq %d 600' % (P0 + 40), 'step 4', 'colors pal',
            'peek F:4A83D 1 view', 'peek F:4A992 2 pass0', 'frame extracted/captures/followup/fc-%s-000.ppm' % kind]
    if kind != 'none': out.append('poke F:4A943 1 %d' % (2 if kind.startswith('flare') else 1))
    for k in range(1, 61):
        out.append('step 1')
        out.append('peek F:4A992 2 f%d_pass' % k)
        out.append('peek F:4AD2A 192 f%d_pool' % k)
        out.append('peek F:4A943 3 f%d_req' % k)
        out.append('peek F:4AB1A 24 f%d_cam' % k); out.append('peek F:4AC90 18 f%d_mat' % k); out.append('peek F:4AA04 4 f%d_cell' % k); out.append('peek F:4A79E 6 f%d_clip' % k)
        out.append('frame extracted/captures/followup/fc-%s-%03d.ppm' % (kind, k))
    for k in range(61, 400, 12):
        out.append('step 12')
        out.append('frame extracted/captures/followup/fc-%s-%03d.ppm' % (kind, k))
    out += ['peek F:4AC90 18 viewmat', 'peek F:4AB2A 8 camoff', 'peek F:4AB1A 12 campos']

elif kind == 'wrap2':
    # the effect object about 100 du ahead of the player (inside the near level of 176), re-placed before each test from the player's logged start state and velocity
    import struct as _s
    rec0 = player()
    pos = list(_s.unpack('>3i', rec0[0x14:0x18] + rec0[0x18:0x1C] + rec0[0x1C:0x20]))
    vel = list(_s.unpack('>3i', rec0[0x3E:0x42] + rec0[0x42:0x46] + rec0[0x46:0x4A]))
    head()
    for i, t in enumerate((0x7F, 0x40, 0x7FFF, 0x0080, 0x008C, 0x009C, 0x00A0, 0x0100)):
        m = 10 + 14 * i
        out.append('stepuntil F:4A992 2 eq %d 900' % (P0 + m))
        tg = [pos[k] + int((m + 1.5) * vel[k]) for k in range(3)]
        obj = bytearray(32)
        _s.pack_into('>H', obj, 0, 0x40); _s.pack_into('>H', obj, 2, 0x13); _s.pack_into('>H', obj, 4, t & 0xFFFF)
        _s.pack_into('>h', obj, 0x06, tg[0] >> 22); _s.pack_into('>h', obj, 0x08, tg[2] >> 22)
        _s.pack_into('>h', obj, 0x0C, (tg[0] & 0x3FFFFF) >> 8); _s.pack_into('>h', obj, 0x0E, (tg[2] & 0x3FFFFF) >> 8)
        _s.pack_into('>i', obj, 0x10, tg[1] >> 8)
        obj[0x0A] = rec0[0x0A]; obj[0x0B] = rec0[0x0B]
        for j, o in enumerate((0x14, 0x18, 0x1C)): _s.pack_into('>i', obj, o, tg[j])
        for q in range(0, 32, 4): out.append('poke F:%X 4 0x%08X' % (0x4D23C + q, _s.unpack_from('>I', obj, q)[0]))
        out.append('poke F:4A910 1 0xFF')
        for f in range(6):
            out.append('step 3')
            out.append('peek F:4D240 2 w%d_%d_t' % (i, f)); out.append('peek F:4A992 2 w%d_%d_pass' % (i, f)); out.append('peek F:4D23C 8 w%d_%d_obj' % (i, f))
            out.append('frame extracted/captures/followup/w2-%d-%d.ppm' % (i, f))

elif kind == 'wrap':
    import struct as _s
    rec0 = player()
    fwd = _s.unpack('>3h', rec0[0x96:0x98] + rec0[0x9C:0x9E] + rec0[0xA2:0xA4])
    pos = list(_s.unpack('>3i', rec0[0x14:0x18] + rec0[0x18:0x1C] + rec0[0x1C:0x20]))
    tgt = [pos[i] + fwd[i] * (600 * 256) // 0x4000 for i in range(3)]
    head()
    obj = bytearray(32)
    _s.pack_into('>H', obj, 0, 0x40); _s.pack_into('>H', obj, 2, 0x13); _s.pack_into('>H', obj, 4, 0x7F)
    _s.pack_into('>h', obj, 0x06, tgt[0] >> 22); _s.pack_into('>h', obj, 0x08, tgt[2] >> 22)
    _s.pack_into('>h', obj, 0x0C, (tgt[0] & 0x3FFFFF) >> 8); _s.pack_into('>h', obj, 0x0E, (tgt[2] & 0x3FFFFF) >> 8)
    _s.pack_into('>i', obj, 0x10, tgt[1] >> 8)
    obj[0x0A] = rec0[0x0A]; obj[0x0B] = rec0[0x0B]
    for i, o in enumerate((0x14, 0x18, 0x1C)): _s.pack_into('>i', obj, o, tgt[i])
    for i in range(0, 32, 4): out.append('poke F:%X 4 0x%08X' % (0x4D23C + i, _s.unpack_from('>I', obj, i)[0]))
    out.append('poke F:4A910 1 0xFF')
    for t in (0x7F, 0x40, 0x7FFF, 0x0080, 0x008C, 0x009C, 0x00A0, 0x0100, 0x7FFF, 0xFFFF):
        out.append('poke F:4D240 2 0x%04X' % t)
        out.append('step 1')
        out.append('peek F:4D240 2 obj_t%04X' % t)
        out.append('frame extracted/captures/followup/wrap-%04X.ppm' % t)
        out.append('step 12')
        out.append('peek F:4D240 2 obj2_t%04X' % t)
        out.append('frame extracted/captures/followup/wrap-%04X-b.ppm' % t)

elif kind in ('eject-cockpit', 'eject-external'):
    ext = kind == 'eject-external'
    head()
    V1 = ['4A83D', '4A86D', '4A968', '4A951', '4A872', '4A87D', '4A850', '4A96A', '4A8F6', '4A8FD', '4A895', '4A8E4', '4A863', '4A866', '4A910', '4A8EB', '4A8EE', '4A8EF', '4A966', '4A8ED']
    V2 = ['4A82C', '4A992', '4AA7A', '4A97A', '4A8E2', '4A9F2', '4A994', '4A996', '4A984', '4AA3C', '4AA3E']
    if ext: out += ['key KPENTER 0.12', 'step 30', 'peek F:4A83D 1 lvl_before', 'poke F:4A83D 1 2', 'step 12', 'peek F:4A83D 1 lvl_poked', 'frame extracted/captures/followup/ej-ext-pre.ppm']   # the external view at level 2 (the level byte poked: the keypad's distance keys set it)
    out += ['keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT']
    for k in range(0, 150):
        out.append('step 1' if k else 'step 1')
        out.append('peek F:%X 128 s%d_r0' % (R(0), k)); out.append('peek F:%X 128 s%d_r1' % (R(1), k))
        for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
        for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
        if k < 100 or k % 4 == 0: out.append('frame extracted/captures/followup/ej-%s-%03d.ppm' % ('ext' if ext else 'cp', k))

elif kind == 'rangekey':
    p0 = player()
    head()
    formation(p0, 320)             # a hostile 320 passes of the player's velocity (about 5 statute miles) ahead, keeping its place
    out += ['poke F:4A910 1 0xFF', 'step 36', 'colors pal', 'peek F:%X 1 r_start' % (R(0) + 0x63), 'frame extracted/captures/followup/rk-000.ppm']
    for k in range(1, 40):
        out.append('key R 0.12')
        out.append('step 3')
        out.append('peek F:%X 1 r%d_n63' % (R(0) + 0x63, k))
        out.append('peek F:4A8EF 1 r%d_ef' % k)
        out.append('step 33')
        out.append('peek F:%X 128 r%d_r0' % (R(0), k)); out.append('peek F:%X 128 r%d_r8' % (R(8), k))
        out.append('frame extracted/captures/followup/rk-%03d.ppm' % k)

elif kind == 'scope':
    p0 = player()
    head()
    pos = [struct.unpack('>i', p0[o:o + 4])[0] for o in (0x14, 0x18, 0x1C)]
    vel = [struct.unpack('>i', p0[o:o + 4])[0] for o in (0x3E, 0x42, 0x46)]
    formation(p0, 64)
    out.append('poke F:4A910 1 0xFF')
    for i, N in enumerate((64, 192, 320, 448)):       # about 1, 3, 5 and 7 statute miles ahead (4,224 list units = 1 mile; one pass of the player's velocity is about 66 of them)
        m = 12 + 36 * i
        out.append('stepuntil F:4A992 2 eq %d 900' % (P0 + m))
        tp = [pos[k] + (m + N) * vel[k] for k in range(3)]
        for o, v in zip((0x14, 0x18, 0x1C), tp): out.append('poke F:%X 4 0x%08X' % (R(8) + o, v & 0xFFFFFFFF))
        out.append('poke F:%X 2 0x%04X' % (R(8) + 6, ((tp[0] & 0xFFFFFFFF) >> 22)))
        out.append('poke F:%X 2 0x%04X' % (R(8) + 8, ((tp[2] & 0xFFFFFFFF) >> 22)))
        out.append('poke F:%X 2 0x%04X' % (R(8) + 0xC, ((tp[0] & 0x3FFFFF) >> 8)))
        out.append('poke F:%X 2 0x%04X' % (R(8) + 0xE, ((tp[2] & 0x3FFFFF) >> 8)))
        out.append('poke F:%X 4 0x%08X' % (R(8) + 0x10, (tp[1] >> 8) & 0xFFFFFFFF))
        out.append('poke F:4A910 1 0xFF')
        out.append('step 40')
        out.append('peek F:%X 128 q%d_r0' % (R(0), i)); out.append('peek F:%X 128 q%d_r8' % (R(8), i))
        out.append('peek F:%X 1 q%d_n63' % (R(0) + 0x63, i)); out.append('peek F:4A992 2 q%d_pass' % i)
        out.append('frame extracted/captures/followup/sc-%d.ppm' % i)
        out.append('step 12')
        out.append('frame extracted/captures/followup/sc-%d-b.ppm' % i)

elif kind == 'firstpass':
    head('aircraft-select')
    out.append('key 1 0.12')
    for k in range(0, 350):
        out.append('step 2')
        for v, n in (('4A8EE', 1), ('4A8EF', 1), ('4A84D', 1), ('4A83D', 1), ('4A992', 2), ('4A8F0', 1)):
            out.append('peek F:%s %d p%d_%s' % (v, n, k, v))
        out.append('peek F:1A6BC 4 p%d_st' % k)

elif kind == 'crashnibble':
    head('dive-precrash')
    V1 = ['4A951', '4A94F', '4A952', '4A83D', '4A84D', '4A850', '4A8E4', '4A863', '4A866', '4A910', '4A968', '4A8EE']
    V2 = ['4A82C', '4A992', '4A97A', '4AA7A', '4A8E2', '4A984', '4A994', '4A996', '4A982']
    out += ['keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT']
    for k in range(0, 260):
        out.append('step 4')
        out.append('peek F:%X 128 s%d_r0' % (R(0), k)); out.append('peek F:%X 32 s%d_r1' % (R(1), k))
        for v in ('4A992', '4A97A', '4A8E2', '4A984', '4A994'): out.append('peek F:%s 2 s%d_%s' % (v, k, v))
        for v in ('4A951', '4A94F', '4A83D', '4A850', '4A8E4'): out.append('peek F:%s 1 s%d_%s' % (v, k, v))
        if k % 8 == 0: out.append('frame extracted/captures/followup/cn-%03d.ppm' % k)

elif kind == 'crashafter':
    # the ejection first (the seat spawns, the nibble holds at 6), then the aircraft is lowered to just above the ground with a descent velocity (record fields only): the crash with the nibble non-zero
    head()
    out += ['keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT', 'stepuntil F:4AA7A 2 ne 0 600', 'step 40',
            'poke F:%X 4 0x%08X' % (R(0) + 0x18, 6000), 'poke F:%X 4 0x%08X' % (R(0) + 0x10, 6000 >> 8), 'poke F:%X 4 0x%08X' % (R(0) + 0x42, (-4376) & 0xFFFFFFFF)]
    for k in range(0, 260):
        out.append('step 4')
        out.append('peek F:%X 128 s%d_r0' % (R(0), k)); out.append('peek F:%X 128 s%d_r1' % (R(1), k))
        for v in ('4A992', '4A97A', '4AA7A', '4A8E2', '4A984', '4A994'): out.append('peek F:%s 2 s%d_%s' % (v, k, v))
        for v in ('4A951', '4A94F', '4A83D', '4A850', '4A8E4', '4A87D'): out.append('peek F:%s 1 s%d_%s' % (v, k, v))
        if k % 8 == 0: out.append('frame extracted/captures/followup/ca-%03d.ppm' % k)

elif kind == 'crashafter2':
    # the dive of the crash captures from a higher start: the height poked to 36,808 position units (about 8 passes of the dive's descent of 4,376 per pass above the floor of 1,800), so that the seat has spawned (the sixth pass) when the aircraft hits
    head('dive-precrash')
    out += ['poke F:%X 4 0x%08X' % (R(0) + 0x18, 36808), 'poke F:%X 4 0x%08X' % (R(0) + 0x10, 36808 >> 8), 'keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT']
    for k in range(0, 260):
        out.append('step 4')
        out.append('peek F:%X 128 z%d_r0' % (R(0), k)); out.append('peek F:%X 128 z%d_r1' % (R(1), k))
        for v in ('4A992', '4A97A', '4AA7A', '4A8E2', '4A984', '4A994'): out.append('peek F:%s 2 z%d_%s' % (v, k, v))
        for v in ('4A951', '4A94F', '4A83D', '4A850', '4A8E4', '4A87D'): out.append('peek F:%s 1 z%d_%s' % (v, k, v))
        if k % 8 == 0: out.append('frame extracted/captures/followup/cb-%03d.ppm' % k)

elif kind == 'seatshot':
    head()
    V1 = ['4A83D', '4A850', '4A8E4', '4A863', '4A910', '4A968']
    V2 = ['4A992', '4AA7A', '4A97A', '4A8E2', '4A984', '4A994', '4A996']
    out += ['keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT', 'stepuntil F:4AA7A 2 ne 0 600']
    out += ['step 20']
    out.append('# the seat (record 1) is marked crashed the way a hit marks it (the first run of this part left it flying: the crashed bit alone is not converted for a seat), then dying (0x0400)')
    out.append('poke F:%X 2 0x14CA' % (R(1) + 0))
    for k in range(0, 160):
        out.append('step 2')
        out.append('peek F:%X 128 s%d_r0' % (R(0), k)); out.append('peek F:%X 128 s%d_r1' % (R(1), k))
        for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
        for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))

elif kind == 'eject-external-long':
    # the external view at level 2 (poked), SHIFT-E, then 4 frames per sample for about 160 passes: when does the seat's step switch the view, and what becomes of the level byte
    head()
    out += ['key KPENTER 0.12', 'step 30', 'poke F:4A83D 1 2', 'step 12', 'keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT']
    for k in range(0, 330):
        out.append('step 4')
        out.append('peek F:%X 32 s%d_r1' % (R(1), k))
        out.append('peek F:%X 4 s%d_r0w' % (R(0), k)); out.append('peek F:%X 1 s%d_nib' % (R(0) + 0x7C, k)); out.append('peek F:4A863 1 s%d_4A863' % k)
        for v, n in (('4A992', 2), ('4A994', 2), ('4A996', 2), ('4A83D', 1), ('4A8EB', 1), ('4A968', 1), ('4A8EE', 1), ('4A8EF', 1), ('4A966', 1), ('4A96A', 1), ('4AA3C', 2), ('4A8E2', 1)):
            out.append('peek F:%s %d s%d_%s' % (v, n, k, v))
        if k % 15 == 0: out.append('frame extracted/captures/followup/el-%03d.ppm' % k)

elif kind in ('gunkillpost', 'gunkillpost-ext'):
    # the gun kill of a designated target, per video frame around the third hit: the post code V:4AB98 (0x4016 in the draw handler), the message latch, the designation and the lamp bits
    p0 = player()
    head()
    rec = formation(p0, 12 if kind == 'gunkillpost' else 3)       # the external run: the MiG about 200 du ahead, near enough to be drawn in the chase view
    out.append('poke F:%X 1 0x02' % (R(8) + 0x3C))              # two hits taken: the next tracer hit is the third
    sel = 0x10 | (p0[0x63] & 0x0F)
    out.append('poke F:%X 1 0x%02X' % (R(0) + 0x63, sel))
    out.append('poke F:%X 1 0x44' % (R(0) + 0x5F))
    out.append('poke F:4AA78 2 0x1000')
    if kind == 'gunkillpost-ext': out += ['key KPENTER 0.12', 'step 20', 'poke F:4AA78 2 0x1000']        # the external (chase) view: the gun runs before the entity pass in it (7.3, 12.5)
    out.append('keydown SPACE')
    out.append('stepuntil F:%X 2 and 0x0200 900' % R(8))
    for k in range(0, 60):
        for v, n in (('4AB98', 2), ('4A984', 2), ('4AA78', 2), ('54E72', 2), ('4A992', 2), ('4AC08', 2), ('4AC0C', 2), ('4A93D', 1), ('4A926', 1), ('4A925', 1), ('4A920', 1), ('4A96C', 1)):
            out.append('peek F:%s %d g%d_%s' % (v, n, k, v))
        out.append('peek F:%X 128 g%d_r8' % (R(8), k))
        out.append('step 1')
    out.append('keyup SPACE')

elif kind == 'parachute':
    p0 = player()
    head()
    V1 = ['4A83D', '4A86D', '4A968', '4A87D', '4A850', '4A8E4', '4A863', '4A910']
    V2 = ['4A992', '4AA7A', '4A97A', '4A8E2', '4A984', '4A994', '4A996']
    out.append('poke F:%X 1 0x%02X' % (R(0) + 0x7C, (p0[0x7C] & 0xF0) | 0x0F))     # the ejection nibble 15: the seat routine turns 15 (14 after the player's step) into the parachute
    for k in range(0, 300):
        out.append('step 12')
        for r in (0, 1, 2): out.append('peek F:%X 128 s%d_r%d' % (R(r), k, r))
        for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
        for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
        if k % 20 == 0: out.append('frame extracted/captures/followup/pc-%03d.ppm' % k)

elif kind == 'launchorder':
    p0 = player()
    head()
    out.append('poke F:4A873 1 1')
    out.append('poke F:4A874 1 1')
    for k in range(0, 60):
        out.append('step 1')
        for r in range(1, 12):
            out.append('peek F:%X 128 s%d_r%d' % (R(r), k, r)) if r in (1, 2, 3, 5, 9, 11) else None
        for v, n in (('4A873', 1), ('4A874', 1), ('4A992', 2), ('4A872', 1), ('4A87D', 1)):
            out.append('peek F:%s %d s%d_%s' % (v, n, k, v))

elif kind in ('threat', 'threat1'):
    txt = subprocess.run([sys.executable, 'tools/vamiga-ref/scripts/gen-combat-cm.py', 'missile', '90'], capture_output=True, text=True, check=True).stdout.splitlines()
    for l in txt:
        if kind == 'threat1' and l.startswith('poke F:%X 4 ' % (R(9) + 0x60)):      # the long that holds +0x60..+0x63: the class byte +0x62 becomes 1 (the long-range missile)
            v = int(l.split()[3], 16); v = (v & 0xFFFF00FF) | 0x0100; l = 'poke F:%X 4 0x%08X' % (R(9) + 0x60, v)
        out.append(l)
        if l.startswith('peek F:4A992 2 '):
            out.append(l.replace('F:4A992 2', 'F:4A925 1').replace('_4A992', '_4A925'))
            out.append(l.replace('F:4A992 2', 'F:4A926 1').replace('_4A992', '_4A926'))
            out.append(l.replace('F:4A992 2', 'F:4AC08 2').replace('_4A992', '_4AC08'))
            out.append(l.replace('F:4A992 2', 'F:4AC0C 2').replace('_4A992', '_4AC0C'))
else:
    sys.exit('unknown kind %s' % kind)
print('\n'.join(x for x in out if x))

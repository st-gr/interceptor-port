#!/usr/bin/env python3
"""Generates the hit captures of note 07 section 8 (combat-hit-*.txt: the player's weapons against a hostile).

    python3 gen-combat-hit.py demo    [samples] [step_frames]   > combat-hit-demo.txt      the demo's own AIM-120 followed to its end
    python3 gen-combat-hit.py gun     [samples] [step_frames]   > combat-hit-gun.txt       a placed MiG-29 ahead of the airborne player, the gun held
    python3 gen-combat-hit.py missile [samples] [step_frames]   > combat-hit-missile.txt   the same with the Sidewinder, the MiG designated (it passes the MiG)
    python3 gen-combat-hit.py demolong [samples] [step_frames] > combat-hit-demolong.txt  the demo's AIM-120 followed past its hit to the kill count (records 1 and 10 only)
    python3 gen-combat-hit.py gunkill [samples] [step_frames]   > combat-hit-gunkill.txt   the gun capture with the MiG's hit counter +0x3C poked to 2 (the next hit is the third)
    python3 gen-combat-hit.py missilehit [samples] [step_frames] > combat-hit-missilehit.txt  the Sidewinder at a MiG 120 passes of the player's velocity ahead

Run under ulimit -v 2500000 from the repository root (gun and missile use the Musashi rig to get the fields of the mode-3 placement of record 8, and
need the log of combat-dying-probe.txt for the airborne snapshot's player record).  The staged record 8 is the placed MiG-29's first 0xA4 bytes
(the fields, never the model vertices behind them) with the player's attitude and a position N passes of the player's velocity ahead; its descriptor is the
template of the placement (the `copy` command moves its twenty bytes inside the machine).  The log record of the flight is read at R:C54498.
"""
import sys, struct, os, re
sys.path.insert(0, 'tools/vamiga-ref'); sys.path.insert(0, 'tools/m68k-rig/tests/lists_model')
kind = sys.argv[1] if len(sys.argv) > 1 else 'demo'
n = int(sys.argv[2]) if len(sys.argv) > 2 else 150
step = int(sys.argv[3]) if len(sys.argv) > 3 else 3
R = lambda i: 0x4B23C + 0x200 * i
LOGREC = 'C54498'
out = []
if kind in ('demo', 'demolong'):
    out += ['restore extracted/captures/demo.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze',
            'stepuntil F:54E5C 4 eq 0xC5473F 9000']
else:
    out += ['restore extracted/captures/airborne.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
if kind not in ('demo', 'demolong'):
    import logparse
    from combat import stage, REC, rig
    r = stage(mode=3)
    placed = bytearray(r.peek(REC(8), 0xA4))
    L = logparse.load('extracted/captures/logs/combat-dying-probe.txt')
    p0 = bytes(L['rec0'].data)
    rec = placed
    def w16(o, v): rec[o:o + 2] = struct.pack('>H', v & 0xFFFF)
    def w32(o, v): rec[o:o + 4] = struct.pack('>I', v & 0xFFFFFFFF)
    rec[0x92:0xA4] = p0[0x92:0xA4]                               # the player's attitude words
    pos = [struct.unpack('>i', p0[o:o + 4])[0] for o in (0x14, 0x18, 0x1C)]
    vel = [struct.unpack('>i', p0[o:o + 4])[0] for o in (0x3E, 0x42, 0x46)]
    N = {'missile': 30, 'missilehit': 120}.get(kind, 12)
    tp = [pos[k] + N * vel[k] for k in range(3)]
    w16(0, 0x11C0); w16(2, 0); rec[5] = 0; rec[0x20] = 0; rec[0x38] = 0xFF; rec[0x5E] = 3
    w16(0x2C, 0xFFFF); w16(0x4C, 40)
    if kind == 'gunkill': rec[0x3C] = 0x02
    w16(0x6C, 0x0800); w16(0x6E, 0x0800)
    for o in (0x3E, 0x42, 0x46): w32(o, 0)
    w32(0x14, tp[0]); w32(0x18, tp[1]); w32(0x1C, tp[2])
    w16(6, (tp[0] & 0xFFFFFFFF) >> 22); w16(8, (tp[2] & 0xFFFFFFFF) >> 22)
    w16(0xC, (tp[0] & 0x3FFFFF) >> 8); w16(0xE, (tp[2] & 0x3FFFFF) >> 8); w32(0x10, tp[1] >> 8)
    # the descriptor of record 8 is the template of the placement (the template table at 0x25ECC, template 6), copied inside the machine into the live descriptor at 0x2600C + 20 * 8:
    # the script names addresses only and carries no bytes of the game
    out.append('copy F:%X F:%X 20' % (0x25ECC + 20 * 6, 0x2600C + 20 * 8))
    for o in range(0, 0xA4, 4):
        out.append('poke F:%X 4 0x%08X' % (R(8) + o, struct.unpack_from('>I', rec, o)[0]))
    sel = (0x10 if kind.startswith('gun') else 0x20) | (p0[0x63] & 0x0F)
    out.append('poke F:%X 1 0x%02X' % (R(0) + 0x63, sel))
    out.append('poke F:%X 1 0x44' % (R(0) + 0x5F))
V1 = ['4A963', '4A961', '4A910', '4A872', '4A87D', '4A920']
V2 = ['4A992', '4AA78', '4A97E', '54E72']
def sample(k):
    out.append('peek F:%X 128 s%d_r0' % (R(0), k))
    for r in ((1, 10) if kind == 'demolong' else (1, 8, 10) if kind in ('demo', 'missile', 'missilehit') else (8,)):
        out.append('peek F:%X 128 s%d_r%d' % (R(r), k, r))
    if not kind.startswith('missile') and kind != 'demolong': out.append('peek F:4AD2A 640 s%d_pool' % k)
    out.append('peek R:%s 80 s%d_log' % (LOGREC, k))
    for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
    for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
out.append('peek F:1D024 4 recptr')
for k in range(n):
    if kind.startswith('missile') and k < 4: out.append('poke F:4AA78 2 0x1000')
    if kind.startswith('missile') and k == 3: out.append('key SPACE 0.12')
    if kind.startswith('gun') and k == 3: out.append('keydown SPACE')
    sample(k)
    out.append('step %d' % step)
if kind.startswith('gun'): out.append('keyup SPACE')
print('\n'.join(out))

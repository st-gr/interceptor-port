#!/usr/bin/env python3
"""Generates the countermeasure and player-damage capture scripts of note 07 sections 9 and 10 (combat-cm-*.txt, combat-hit-player*.txt).

    python3 gen-combat-cm.py keys    [samples] > combat-cm-keys.txt         F, C and J in flight from airborne.vasnap, per frame
    python3 gen-combat-cm.py hit     [samples] [even|odd] > combat-hit-player-even.txt   a staged stationary aircraft on the player's track, the frame counter pinned
    python3 gen-combat-cm.py hitflare [samples] [even|odd] > combat-hit-player-flare-even.txt   the same with a flare released at frame 1
    (combat-hit-player.txt is the first, hand-staged form: a parallel-flying copy given group 3 at sample 5; it hit by luck of the timing)
    python3 gen-combat-cm.py missile [samples] > combat-cm-missile.txt     a staged hostile Sidewinder 45 passes of the player's velocity behind the player
    python3 gen-combat-cm.py missileflare [samples] > combat-cm-missile-flare.txt   the same with a flare released at frame 1

hit, hitflare, missile and missileflare need the log of combat-dying-probe.txt (the airborne snapshot's player record).  The measurements are per frame: the
player's W0, W2, B20, indicated speed, the posted code V:4AB98, V:4A984, the jolt byte V:4A878, the cool-downs V:4A907 and V:4A906, the frame counter V:4A992, the
counters V:4A904 to V:4A945.
"""
import sys, struct
sys.path.insert(0, 'tools/vamiga-ref')
kind = sys.argv[1] if len(sys.argv) > 1 else 'keys'
n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
R = lambda i: 0x4B23C + 0x200 * i
out = ['restore extracted/captures/airborne.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
V1 = ['4A904', '4A905', '4A906', '4A907', '4A943', '4A944', '4A945', '4A96D', '4A878', '4A87D', '4A952', '4A844', '4A8F8', '4A865']
V2 = ['4AB98', '4A984', '4A992', '4B29C']


def sample(k, with_pool=True, recs=(0,)):
    for r in recs:
        out.append('peek F:%X 128 s%d_r%d' % (R(r), k, r))
    for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
    for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
    out.append('peek F:4B2B9 1 s%d_e7d' % k)
    if with_pool: out.append('peek F:4AD2A 256 s%d_pool' % k)


if kind == 'keys':
    # baseline, then F (3 samples later C, later J)
    for k in range(n):
        if k == 3: out += ['key F 0.12']
        if k == 15: out += ['key C 0.12']
        if k == 27: out += ['key J 0.12']
        sample(k)
        out.append('step 1')
elif kind in ('missile', 'missileflare'):
    import logparse
    L = logparse.load('extracted/captures/logs/combat-dying-probe.txt')
    rec = bytearray(L['rec0'].data)
    def w16(o, v): rec[o:o+2] = struct.pack('>H', v & 0xFFFF)
    def w32(o, v): rec[o:o+4] = struct.pack('>I', v & 0xFFFFFFFF)
    pos = [struct.unpack('>i', rec[o:o+4])[0] for o in (0x14, 0x18, 0x1C)]
    vel = [struct.unpack('>i', rec[o:o+4])[0] for o in (0x3E, 0x42, 0x46)]
    mp = [pos[k] - 45 * vel[k] for k in range(3)]
    w16(0, 0x10C3); w16(2, 0); rec[0x62] = 0; rec[0x38] = 0x80; rec[0x5E] = 3; rec[0x5D] = 0xFF; rec[5] = 0; rec[0x20] = 0
    w16(0x4C, 150); w16(0x26, 0x14); w16(0x6C, 0x4200); w16(0x6E, 0x4200); w16(0x2C, 0xFFFF)
    w32(0x14, mp[0]); w32(0x18, mp[1]); w32(0x1C, mp[2])
    w16(6, (mp[0] & 0xFFFFFFFF) >> 22); w16(8, (mp[2] & 0xFFFFFFFF) >> 22)
    w16(0xC, ((mp[0] & 0x3FFFFF) >> 8)); w16(0xE, ((mp[2] & 0x3FFFFF) >> 8)); w32(0x10, mp[1] >> 8)
    for o in range(0, 0xA4, 4):
        out.append('poke F:%X 4 0x%08X' % (R(9) + o, struct.unpack_from('>I', rec, o)[0]))
    for k in range(n):
        if kind == 'missileflare' and k == 1: out += ['key F 0.12']
        sample(k, with_pool=True, recs=(0, 9))
        out.append('step 1')
else:
    # hit / hitflare [samples] [parity: even|odd]: a stationary inactive aircraft record (valid, not active, group 3) is put on the player's straight-line track 6
    # passes ahead (the player's position plus six times its velocity longs); the frame counter V:4A992 is poked to a fixed value every sample so that the
    # parity that the damage handler reads is chosen (even 0x00E8, odd 0x00E9).  An inactive record is not marked by the hit, so the contact would repeat while the
    # boxes overlap; the player passes the box in one pass.
    import logparse
    L = logparse.load('extracted/captures/logs/combat-dying-probe.txt')
    rec = bytearray(L['rec0'].data)
    def w16(o, v): rec[o:o+2] = struct.pack('>H', v & 0xFFFF)
    def w32(o, v): rec[o:o+4] = struct.pack('>I', v & 0xFFFFFFFF)
    parity = sys.argv[3] if len(sys.argv) > 3 else 'even'
    pos = [struct.unpack('>i', rec[o:o+4])[0] for o in (0x14, 0x18, 0x1C)]
    vel = [struct.unpack('>i', rec[o:o+4])[0] for o in (0x3E, 0x42, 0x46)]
    tp = [pos[k] + 6 * vel[k] for k in range(3)]
    w16(0, 0x0044); w16(2, 0)
    rec[0x62] = 0x12; rec[0x63] = 0x06; rec[0x5E] = 3; rec[5] = 0; rec[0x20] = 0; rec[0x38] = 0xFF; rec[0x5D] = 0xFF
    w16(0x2C, 0xFFFF); w16(0x4C, 40); w16(0x6C, 0); w16(0x6E, 0)
    for o in (0x3E, 0x42, 0x46): w32(o, 0)
    w32(0x14, tp[0]); w32(0x18, tp[1]); w32(0x1C, tp[2])
    w16(6, (tp[0] & 0xFFFFFFFF) >> 22); w16(8, (tp[2] & 0xFFFFFFFF) >> 22)
    w16(0xC, (tp[0] & 0x3FFFFF) >> 8); w16(0xE, (tp[2] & 0x3FFFFF) >> 8); w32(0x10, tp[1] >> 8)
    for o in range(0, 0xA4, 4):
        out.append('poke F:%X 4 0x%08X' % (R(8) + o, struct.unpack_from('>I', rec, o)[0]))
    for k in range(n):
        if kind == 'hitflare' and k == 1: out += ['key F 0.12']
        out.append('poke F:4A992 2 0x%04X' % (0xE8 if parity == 'even' else 0xE9))
        sample(k, with_pool=(kind == 'hitflare'), recs=(0, 8))
        out.append('step 1')
print('\n'.join(out))

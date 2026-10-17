#!/usr/bin/env python3
"""Generates the dying-path captures of note 07 section 5 (combat-dying-*.txt).

    python3 gen-combat-dying.py air [dest] [samples] > combat-dying-air.txt
    python3 gen-combat-dying.py low [dest] [samples] > combat-dying-low.txt

Needs the log of combat-dying-probe.txt (the airborne snapshot's player record).  The staging copies the player record into record 8, makes it
a counted MiG-29-class aircraft (class 0x12, valid, active, not in any queue so that it is not drawn), and at sample 10 pokes the hit state that the
gun's third hit (0x2B286, 0x2B2A4) leaves: W0 bit 0x0200 and the out-of-control bit of +0x20.  `low` lowers it to 1,400 units above the ground first, so
that it reaches the ground inside the 15-pass dying count.  `dest` keeps the destination words of the player copy (0) or sets them to "none" (the
default).
"""
import sys, struct
sys.path.insert(0, 'tools/vamiga-ref')
import logparse
kind = sys.argv[1] if len(sys.argv) > 1 else 'air'
dest = len(sys.argv) > 2 and sys.argv[2] == 'dest'
n = int(sys.argv[3]) if len(sys.argv) > 3 else 90
L = logparse.load('extracted/captures/logs/combat-dying-probe.txt')
rec = bytearray(L['rec0'].data)
def w16(o, v): rec[o:o+2] = struct.pack('>H', v & 0xFFFF)
def w32(o, v): rec[o:o+4] = struct.pack('>I', v & 0xFFFFFFFF)
w16(0, 0x10C4)                       # valid, active, hits the AI step, not in any queue; no 0x0008: a counted hostile
w16(2, 0)
rec[0x62] = 0x12; rec[0x63] = 0x06; rec[0x5E] = 3; rec[5] = 0; rec[0x20] = 0; rec[0x38] = 0xFF
if not dest:
    w16(0x2C, 0xFFFF)
w16(0x4C, 40)
if kind == 'low':
    w32(0x18, 1400)
R = 0x4B23C + 0x200 * 8
out = ['restore extracted/captures/airborne.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
for o in range(0, 0xA4, 4):      # only the first 0xA4 bytes (the fields; never the model vertices behind them)
    out.append('poke F:%X 4 0x%08X' % (R + o, struct.unpack_from('>I', rec, o)[0]))
out.append('poke F:%X 4 0x%08X' % (0x4B23C + 0x18, 3000000))    # the player's altitude, so that its own descent does not end the capture
V1 = ['4A963', '4A910', '4A961']
def sample(k):
    out.append('peek F:%X 128 s%d_r8' % (R, k))
    out.append('peek F:4A992 2 s%d_n' % k)
    out.append('peek F:4AA78 2 s%d_lock' % k)
    for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
    out.append('peek F:%X 2 s%d_r8w0' % (R, k))
for k in range(n):
    sample(k)
    if k == 10:
        out.append('poke F:%X 2 0x%04X' % (R, 0x12C4))
        out.append('poke F:%X 1 0x02' % (R + 0x20))
    out.append('step 1')
print('\n'.join(out))

#!/usr/bin/env python3
"""Generates combat-fx-burst.txt (note 07 sections 11 and 13): staged captures of the dying burst, the smoke trail and an effect object on the reference emulator.

    python3 gen-combat-fx.py > combat-fx-burst.txt

Part 1 (burst and trail): from extracted/captures/airborne.vasnap a copy of the player's record (the first 0xA4 bytes, the player's start state as logged by
flow-egg-sweep-0.txt) is written into record 4 as a MiG-29 (class 0x12) 600 units straight ahead of the nose, with the template descriptor of model 6 (the `copy` command, inside the emulated machine), given the state a gun or
missile kill leaves behind (W0 crashed bit 0x0200, out of control, W2 trail bit 0x1000, state 1; V:54E72 is already record 4's offset 0x800); the frames of the next 120 video frames
are saved every 4 frames, with the record's W0, counter +0x4C, trail length +0x3D and the ring variables logged each frame.
Part 2 (an effect object): the snapshot is restored again and an object (id 0x13 = model 35) is written into the first slot of the object table 600 units ahead; its life word is
poked to chosen values and one frame is saved for each (growth, steady, decay and spent).
Inputs (git-ignored): extracted/captures/airborne.vasnap, extracted/captures/logs/flow-egg-sweep-0.txt (the player's record), extracted/interceptor.bin.
"""
import struct, sys
ROOT = '/sandbox/interceptor-wasm/'
img = open(ROOT + 'extracted/interceptor.bin', 'rb').read()
rec0 = None
for l in open(ROOT + 'extracted/captures/logs/flow-egg-sweep-0.txt'):
    if l.startswith('c0_r '):
        rec0 = bytes.fromhex(''.join(l.split()[2:])); break
assert rec0 and len(rec0) >= 0xA4
def iw(a): return struct.unpack('>H', img[a - 0x10000:a - 0x10000 + 2])[0]
def model_nibble(tpl):
    base_ = struct.unpack('>I', tpl[4:8])[0]
    w0 = iw(base_)
    d = w0 if w0 & 0x8000 else (iw(base_ + 2) if w0 & 0x4000 else iw(base_ + 4))
    return img[base_ + 6 + (d & 0xFFF) - 0x10000] & 0xF
fwd = struct.unpack('>3h', rec0[0x96:0x98] + rec0[0x9C:0x9E] + rec0[0xA2:0xA4])
pos = list(struct.unpack('>3i', rec0[0x14:0x18] + rec0[0x18:0x1C] + rec0[0x1C:0x20]))
DIST = 600 * 256
tgt = [pos[i] + fwd[i] * DIST // 0x4000 for i in range(3)]
out = ['restore extracted/captures/airborne.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
def pokeb(addr, data):
    for i in range(0, len(data), 4):
        chunk = data[i:i + 4].ljust(4, b'\0')
        out.append('poke F:%X 4 0x%s' % (addr + i, chunk.hex().upper()))
tpl = img[0x25ECC - 0x10000 + 20 * 6:0x25ECC - 0x10000 + 20 * 6 + 20]
rec = bytearray(rec0[:0xA4])
struct.pack_into('>H', rec, 0, 0x11C0 | 0x0200)
struct.pack_into('>H', rec, 2, 0x1000)
rec[0x04] = 0; rec[0x05] = 1
for i, o in enumerate((0x14, 0x18, 0x1C)): struct.pack_into('>i', rec, o, tgt[i])
struct.pack_into('>h', rec, 0x06, tgt[0] >> 22); struct.pack_into('>h', rec, 0x08, tgt[2] >> 22)
struct.pack_into('>h', rec, 0x0C, (tgt[0] & 0x3FFFFF) >> 8); struct.pack_into('>h', rec, 0x0E, (tgt[2] & 0x3FFFFF) >> 8)
struct.pack_into('>i', rec, 0x10, tgt[1] >> 8)
rec[0x20] = 0x02                                          # out of control
rec[0x5E] = 4; rec[0x62] = 0x12; rec[0x7A] = 3; rec[0x7D] = model_nibble(tpl)
struct.pack_into('>h', rec, 0x2C, -1)                    # no destination
rec[0x38] = 0xFF
struct.pack_into('>H', rec, 0x4C, 0)
VARS = [('4B23C', 2), ('4B23E', 2), ('4BA3C', 2), ('4BA3E', 2), ('4BA4C', 2), ('4BA79', 1), ('4BA7B', 1), ('54E70', 2), ('4A992', 2)]
def sample(tag):
    for a, n in VARS: out.append('peek F:%s %d %s_%s' % (a, n, tag, a))
    out.append('peek F:4BA7C 2 %s_trail' % tag)
    out.append('peek F:54E74 72 %s_ring' % tag)
out.append('copy F:%X F:%X 20' % (0x25ECC + 20 * 6, 0x2600C + 20 * 4))   # the template descriptor of model 6 into the descriptor of record 4, copied inside the machine: the script carries no bytes of the game
pokeb(0x4BA3C, bytes(rec))
out.append('poke F:4A910 1 0xFF')
sample('b0')
for k in range(1, 121):
    out.append('step 1')
    if k % 4 == 0: out.append('frame extracted/captures/fx-burst-%03d.ppm' % k)
    sample('b%d' % k)
# part 2: an effect object, id 0x13, slot 0, 600 units ahead
out += ['restore extracted/captures/airborne.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
obj = bytearray(32)
struct.pack_into('>H', obj, 0, 0x40); struct.pack_into('>H', obj, 2, 0x13); struct.pack_into('>H', obj, 4, 0x7F)
for i, o in enumerate((0x14, 0x18, 0x1C)): pass
struct.pack_into('>h', obj, 0x06, tgt[0] >> 22); struct.pack_into('>h', obj, 0x08, tgt[2] >> 22)
struct.pack_into('>h', obj, 0x0C, (tgt[0] & 0x3FFFFF) >> 8); struct.pack_into('>h', obj, 0x0E, (tgt[2] & 0x3FFFFF) >> 8)
struct.pack_into('>i', obj, 0x10, tgt[1] >> 8)
obj[0x0A] = rec0[0x0A]; obj[0x0B] = rec0[0x0B]
struct.pack_into('>i', obj, 0x14, tgt[0]); struct.pack_into('>i', obj, 0x18, tgt[1]); struct.pack_into('>i', obj, 0x1C, tgt[2])
pokeb(0x4D23C, bytes(obj))
out.append('poke F:4A910 1 0xFF')
for t in (0x7F, 0x7A, 0x60, 0x40, 0x0A, 0x04, 0xFFFF):
    out.append('poke F:4D240 2 0x%04X' % t)
    out.append('step 1')
    out.append('peek F:4D240 2 obj_t%04X' % t)
    out.append('frame extracted/captures/fx-object-%04X.ppm' % t)
print('\n'.join(out))

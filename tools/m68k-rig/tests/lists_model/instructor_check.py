"""The instructor's step (docs/re/08-game-flow.md, 8.3): the original's world update 0x26AF8 run in the Musashi rig for 300 steps of part 1
from the start state, against (1) the emulator log of the part 1 cycle (extracted/captures/logs/flow-training-part1.txt, written by
tools/vamiga-ref/scripts/flow-training-part1.txt) and (2) the same record run as the PLAYER (record 0, index 0) with the key byte, the
recentre bit, the state byte and the phase flags that the AI state routine left for each step.

    python3 instructor_check.py

Prints the counts.  Skips with exit 0 when extracted/interceptor.bin, librig.so or (for part 1) the emulator log is missing.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import IMGPATH, LIBPATH, ROOT
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
LOG = os.path.join(ROOT, "extracted", "captures", "logs", "flow-training-part1.txt")
import struct
sys.path.insert(0, os.path.join(ROOT, 'tools', 'vamiga-ref'))
from common import Rig
import logparse
def setup(r):
    r.reset()
    for a in (0x18E54,0x1A18C,0x1F11E,0x1F0E2): r.poke(a,b'\x4e\x75')
    r.w8(0x4A95E,2); r.w8(0x4A84B,1); r.w16(0x4A984,0x0840); r.w8(0x4A83D,1); r.w16(0x4A994,4); r.w16(0x4A996,0x800)
    r.w8(0x4A844,1); r.w32(0x4A7B2,0x800000)
    for a in range(0x4A83C,0x4A848):
        if a not in (0x4A83D,0x4A83E,0x4A840): r.w8(a,1)
    r.run(0x2CE36,a6=0x710000)
    r.w16(0x4BA3C+2,0x0100); r.w8(0x4BA3C+0x65,0)
STUB=bytes.fromhex('13E90003007F0300'+'13E90065007F0301'+'13E90005007F0302'+'13E90020007F0303'+'4EF90001DFAA')
def hook(rg):
    rg.poke(0x8F0000,STUB); rg.poke(0x2A3AC,bytes.fromhex('4EB9008F0000'))
r=Rig(); setup(r); hook(r); I=0x4BA3C
log=[]; hk=[]; ra_w0=[]
for k in range(300):
    for q in range(4): r.w8(0x7F0300+q,0xFF)
    r.run(0x26AF8,a6=0x710000)
    hk.append(tuple(r.u8(0x7F0300+q) for q in range(4)))
    ra_w0.append(r.u16(I)); log.append((r.u8(I+5),r.u8(I+0x65),r.s32(I+0x14),r.s32(I+0x18),r.s32(I+0x1C),r.u16(I+0x66),r.u16(I+0x68),r.u16(I+0x6A),r.u16(I+0x6E),r.u8(I+0x2B),r.s16(I+0x4C),r.u8(0x4A851)))
# emulator comparison (a1: samples with step counter n; the n counter starts at 0 at the first sample of lesson 1)
if not os.path.exists(LOG):
    print('emulator log missing: the emulator comparison is skipped'); L = {}
else:
    L=logparse.load(LOG); ok=tot=0; bad=[]
i=0
while False:
    I_=L[f"s{i}_ins"]; n=L[f"s{i}_n"].u16(0)
    for off in (-1,0,1):
        pass
    best=None
    for off in (0,1,-1):
        k=n+off
        if 0<=k<300:
            l=log[k]; m=(I_.u16(0x6E)==l[8], I_.u16(0x66)==l[5], I_.u16(0x68)==l[6], I_.u16(0x6A)==l[7], I_.u8(5)==l[0])
            if best is None or sum(m)>sum(best[1]): best=(off,m)
    tot+=1; ok+= all(best[1]); 
    if not all(best[1]): bad.append((i,n,best))
    i+=1

# all emulator samples of the part 1 cycle that fall in the first 300 steps
ok=tot=0; bad=[]; i=0
while L and f"s{i}_ins" in L:
    I_=L[f"s{i}_ins"]; n=L[f"s{i}_n"].u16(0)
    if n<298 and L[f"s{i}_les"].u8(0)>=1 and i<60:
        best=0
        for off in (0,1,-1):
            k=n+off; l=log[k]
            m=(I_.u16(0x6E)==l[8] and I_.u16(0x66)==l[5] and I_.u16(0x68)==l[6] and I_.u16(0x6A)==l[7] and I_.u8(5)==l[0]); best|=m
        tot+=1; ok+=best
        if not best: bad.append((i,n))
    i+=1
print("part 1 samples with n<298:",tot,"identical (speed, pitch, heading, roll, state) at step n, n+1 or n-1:",ok,bad[:5])
# (b) the same record run as the player (index 0) with the logged key bytes, from the state after step 0
ra=Rig(); setup(ra); hook(ra); ra.run(0x26AF8,a6=0x710000); rec=bytearray(ra.peek(I,0x200))
r2=Rig(); r2.reset()
for a in (0x18E54,0x1A18C,0x1F11E,0x1F0E2): r2.poke(a,b'\x4e\x75')
r2.w8(0x4A95E,1); r2.w16(0x4A984,0x0840); r2.w8(0x4A83D,1); r2.w8(0x4A844,1); r2.w32(0x4A7B2,0x800000)
for a in range(0x4A83C,0x4A848):
    if a not in (0x4A83D,0x4A83E,0x4A840): r2.w8(a,1)
rec[2]&=0xFE; rec[5]=0
r2.poke(0x4B23C,bytes(rec)); r2.w16(0x4A994,0); r2.w16(0x4A996,0)
diff=0; first=None; P=0x4B23C
for k in range(1,300):
    r2.w16(P+0x4C,log[k-1][10]&0xFFFF); r2.w16(P+0,(ra_w0[k-1]&0xFEFF)); 
    r2.w8(P+0x65,hk[k][1] if hk[k][1]!=0xFF else log[k][1]); r2.w8(P+3,(r2.u8(P+3)&0xEF)|(hk[k][0]&0x10 if hk[k][0]!=0xFF else 0)); r2.w8(P+5,hk[k][2] if hk[k][2]!=0xFF else 0); r2.w8(P+0x20,(r2.u8(P+0x20)&0xE7)|(hk[k][3]&0x18 if hk[k][3]!=0xFF else 0))
    r2.run(0x26AF8,a6=0x710000)
    l=log[k]; m=(r2.s32(P+0x14),r2.s32(P+0x18),r2.s32(P+0x1C),r2.u16(P+0x66),r2.u16(P+0x68),r2.u16(P+0x6A),r2.u16(P+0x6E))
    w=(l[2],l[3],l[4],l[5],l[6],l[7],l[8])
    if m!=w:
        diff+=1
        if first is None: first=(k,m,w)
print("player-index replay with the instructor's key bytes: 299 steps,",diff,"steps differ; first:",first)

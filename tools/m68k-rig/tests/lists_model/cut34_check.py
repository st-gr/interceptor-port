"""0x21C34 (descriptors for models 67 and 87 = items 51 and 71): draws only when V:4AB1E < -0x280000 (camera above 12,800 ft = 2,621,440 pu / 204.8).
Boundary both ways in the rig and the line it draws (pen, count).   python3 cut34_check.py [SEED] [N]"""
import sys,struct,random,collections
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
import walk_check as W, primlog as P, outlog as F
if inputs_missing(): print('skipped'); sys.exit(0)
seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 60
rng=random.Random(seed); r=Rig(); bad=0; stat=collections.Counter(); pens=collections.Counter()
for k in range(N):
    for item in (51,71):
        W.random_frame(r,rng,camr=300)
        h=rng.choice((0x27FFFF,0x280000,0x280001,0x300000,0x1000000,0x100000,0x2000)) 
        r.w32(0x4AB1E,-h); r.w32(0x4AB30,(-h)>>8)
        tier='A'; es=[e for e in W.window(r,tier) if not r.u8(e+1)&0x50]
        if not es: continue
        e=es[0]
        lod=rng.randint(0,3)
        r.w16(e,(item<<8)|(r.u8(e+1)&0xF0)|lod); r.w32(e+2,0x2600C+20*item)
        cx=struct.unpack('>h',r.peek(0x4AB2A,2))[0]; cz=struct.unpack('>h',r.peek(0x4AB2E,2))[0]
        r.w16(e+6,((-cx)>>lod)+rng.randint(-300,300)); r.w16(e+8,0); r.w16(e+10,((-cz)>>lod)+rng.randint(-300,300))
        r.w16(e+0x10,0); r.w16(e+0x14,0xFFFF); r.w8(e+0x12,3); r.w16(e+24,0xFFFF)
        P.install(r); F.install(r)
        W.walk(r,tier); fl=F.read(r); P.read_log(r)
        res=r.s16(e+0x14); above=(h>0x280000)
        # at or below the cut-off nothing is stored (result 0 -> stored -1 only through the walker's rule) and no output
        out=sum(1 for q in fl if q[0] in (1,2))
        stat[(h>0x280000,out>0)]+=1
        for q in fl:
            if q[0]==2: pens[q[1]]+=1
        if not above and out: bad+=1
        if not above and r.u16(e+0x14)!=0xFFFF and res>0: bad+=1
print('cut34: (camera above 12,800 ft, output drawn) counts',dict(stat),'line pens',dict(pens),'violations',bad)
sys.exit(1 if bad else 0)

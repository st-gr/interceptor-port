"""x handedness (note 03 / 01b): the real camera builder fx_cam_yp (0x32A66) and projection fx_project (0x3377C) in the rig.
For random headings h (clockwise from north, 28800 per circle) a point at bearing h + d (d>0 clockwise, to the right) must land right of centre
with world axes +z north, -x east: direction vector (-sin, 0, cos).   python3 hand_check.py [SEED] [N]"""
import sys, math, random, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
if inputs_missing(): print('skipped'); sys.exit(0)
seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 200
r=Rig(); r.reset(); rng=random.Random(seed)
def s16(v): return v-65536 if v&0x8000 else v
right=left=bad=0; mism=0
for _ in range(N):
    h=rng.randrange(28800); pitch=rng.randint(-2000,2000)
    r.run(0x32A66,d0=pitch&0xFFFF,d2=h,a1=0x70000)       # A = pitch angle, B = heading (note 03)
    M=struct.unpack('>9h',r.peek(0x70000,18))
    d=rng.choice((-1,1))*rng.randint(300,3000)            # +-4..40 degrees (28800 units = 360)
    ang=2*math.pi*(h+d)/28800; dist=rng.randint(200,600)
    w=(-math.sin(ang)*dist, 0, math.cos(ang)*dist)       # +z north, -x east
    c=[ (M[3*i]*w[0]+M[3*i+1]*w[1]+M[3*i+2]*w[2])/256 for i in range(3)]
    r.w16(0x4AA3C,0x7FFF)
    r.run(0x3377C,d0=int(c[0])&0xFFFF,d1=int(c[1])&0xFFFF,d2=int(c[2])&0xFFFF)
    if r.u32(0x4AA10)==0xFFFFFFFF: continue
    sx=r.reg('d0')&0xFFFF
    if (sx>159)==(d>0): right+=1
    else: bad+=1
print('hand_check: %d points projected, screen side agrees with the clockwise bearing in %d, disagrees in %d'%(right+bad,right,bad))
sys.exit(1 if bad else 0)

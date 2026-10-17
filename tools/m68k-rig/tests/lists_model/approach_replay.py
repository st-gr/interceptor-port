"""The free-flight camera approach 0x12D58 replayed in the rig from the emulator's entry state (docs/re/08-game-flow.md, 12.14 N1, C13).

    python3 approach_replay.py RUN [START_N]     RUN 0 = location key 1 (airfield 1), RUN 1 = location key 4 (carrier)

Reads the log extracted/captures/logs/followup-approach-state.txt (tools/vamiga-ref/scripts/followup-approach-state.txt: the variable block
V:4A6E8.., the player record, then the camera, target, trail and stage every 4 blanks), pokes the block and the record into the rig, runs the camera
mover 0x2D756 once per call until the camera mode is 0 and compares the camera with the emulator's sample of each pass.  START_N: the step counter of
the sample to restart from (RUN 1: 30, the first pass after the carrier start's camera reset).  Prints the calls and the matching passes.
Inputs (git-ignored): extracted/interceptor.bin, librig.so, the log.  Run under ulimit -v 2500000; timeout 900.
"""
import os, sys, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH, ROOT
def s32(h):
    v = int(h, 16); return v - (1 << 32) if v >> 31 else v
which=int(sys.argv[1])
txt=open(os.path.join(ROOT,'extracted','captures','logs','followup-approach-state.txt')).read().split('# run ')[1+which]
def blob(lab):
    m=re.search(r'^%s F:\w+ (.*)$'%lab,txt,re.M); return bytes.fromhex(m.group(1).replace(' ',''))
pl=blob('player'); vs=blob('vars')
print(len(pl),len(vs))
d={}
for l in txt.split('\n'):
    m=re.match(r'i(-?\d+)_(\w+) F:\w+ ([0-9A-Fa-f]+)\s*$',l.strip())
    if m: d.setdefault(int(m.group(1)),{})[m.group(2)]=m.group(3)
r=Rig(); r.reset()
for a in (0x321CA,0x18E54,0x1A18C,0x1F11E,0x1F0E2,0x1F1D6,0x1A042,0x1A0DC,0x19C58,0x115F6): r.poke(a,b'\x4e\x75')
r.poke(0x4A6E8,vs); r.poke(0x4B23C,pl)
START=int(sys.argv[2]) if len(sys.argv)>2 else None
if START is not None:
    i=[k for k in sorted(d) if k>=0 and int(d[k]['n'],16)==START][0]
    def put(a,h):
        for j in range(0,len(h),8): r.w32(a+j//2,int(h[j:j+8],16))
    put(0x4ACF6,d[i]['cam']); put(0x4AD0E,d[i]['tgt']); put(0x4AD02,d[i]['trail']); r.w8(0x4A86E,int(d[i]['a86e'],16))
cap={}
for i in sorted(k for k in d if k>=0):
    n=int(d[i]['n'],16); c=d[i]['cam']; cap.setdefault(n,[s32(c[j:j+8]) for j in (0,8,16)])
print('start n',min(cap),'mode',r.u8(0x4A966),'stage',r.u8(0x4A86E),'cam',[r.s32(0x4ACF6+4*i) for i in range(3)],'view',r.u8(0x4A83D),'pause',r.u8(0x4A866))
n=0; stage=None; diffs=0
seq=[]
while n<600:
    r.run(0x2D756); n+=1
    seq.append([r.s32(0x4ACF6+4*i) for i in range(3)])
    s=r.u8(0x4A86E)
    if s!=stage: print('call',n,'stage',s); stage=s
    if r.u8(0x4A966)==0: break
print('rig calls',n)
# compare with capture camera by pass: cap[n0+k] is the camera at the first sample of pass n0+k
n0=START if START is not None else min(cap)
match=0
for k in range(1,len(seq)):
    c=cap.get(n0+k+0)
    if c==seq[k-1]: match+=1
print('matching passes (cap pass n0+k == rig after call k):',match,'of',len(seq))
for k in (1,2,3,10,40,80,len(seq)-1):
    print(k, cap.get(n0+k), seq[k-1] if k-1<len(seq) else None)
bad=[k for k in range(1,len(seq)) if cap.get(n0+k)!=seq[k-1]]
print('mismatch',len(bad),bad[:12])
print('capture last n',max(cap))

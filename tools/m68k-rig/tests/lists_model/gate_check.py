"""V:4A83E (the pitch gate read by ops 39 and 40): the writer 0x143F2.  python3 gate_check.py SEED N"""
import sys, random
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 400
    if inputs_missing(): print('skipped (missing inputs)'); return 0
    rng=random.Random(seed); r=Rig(); bad=0; on=0
    for it in range(N):
        r.reset(); v83d=rng.choice((0,1)); r.w8(0x4A83D,v83d); old=rng.choice((0,1)); r.w8(0x4A83E,old)
        if v83d: r.w16(0x4AB4C,rng.randint(-30000,30000)); p=r.s16(0x4AB4C)>>3
        else: r.w32(0x4AB40,rng.randint(-2**31,2**31-1)//rng.choice((1,4096,65536))); r.w32(0x4AB48,0); p=r.s32(0x4AB40)>>3
        o924=rng.choice((0,1)); r.w8(0x4A924,o924); r.w8(0x4A923,0); r.w8(0x4A910,0)
        r.run(0x143F2,a6=0x710000)
        p=((p+0x8000)&0xFFFF)-0x8000
        g=r.u8(0x4A83E); want=1 if 600<p<3150 else 0
        h=r.u8(0x4A924); wh=o924 if want else (1 if 450<p<3200 else 0)
        if h!=wh: bad+=1; print('DIFF 4A924',p,h,wh) if bad<4 else 0
        on+=g
        if g!=want: bad+=1; 
        if bad and bad<3 and g!=want: print('DIFF',v83d,p,g,want)
    print('gate: %d random states, gate set in %d, %d differences from "V:4A83E = 1 iff the pitch word (V:4AB40 long or V:4AB4C word, shifted right by 3, low word) is in (600,3150); V:4A924 = 1 iff in (450,3200), written only when the gate is off (unchanged inside the gate)"'%(N,on,bad)); return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())

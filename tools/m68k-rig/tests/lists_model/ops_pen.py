"""Ops 39 and 40 (quad / triangle on absolute slots 0..4): vertex variant, pen half, second polygon, return.  python3 pen_check.py SEED N"""
import sys, random, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
import handler_hook as hook
def w16(x): x&=0xffff; return x-0x10000 if x&0x8000 else x
def add(*a): return tuple(w16(sum(c)) for c in zip(*a))
def sub(a,b): return tuple(w16(x-y) for x,y in zip(a,b))
ARR=0x4D43C; STREAM=0x700000
def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 400
    if inputs_missing(): print('skipped (missing inputs)'); return 0
    rng=random.Random(seed); r=Rig(); bad=0; cnt={}
    for it in range(N):
        for op in (39,40):
            r.reset(); r.poke(0x34A28,b'\x4e\x75'); hook.install(r)
            vals=[]
            for k in range(8):
                z=rng.randint(30,1500) if rng.random()<.85 else rng.randint(-500,1500)
                lim=max(z,0)+200
                vals+= [rng.randint(-lim,lim),rng.randint(-lim,lim),z]
            if rng.random()<.5:                                  # slot 1 inside the pyramid more often
                z=vals[5]=rng.randint(50,1500); vals[3]=rng.randint(-z,z); vals[4]=rng.randint(-z,z)
            r.poke(ARR,b''.join(struct.pack('>h',x) for x in vals))
            px=rng.randint(-300,300); pz=rng.randint(-300,300); lod=rng.randint(0,3); cx=rng.randint(-300,300); cz=rng.randint(-300,300)
            r.w16(0x4ABE2,px); r.w16(0x4ABE6,pz); r.w16(0x4AB70,lod); r.w16(0x4AB2A,cx); r.w16(0x4AB2E,cz)
            gate=rng.choice((0,1)); r.w8(0x4A83E,gate)
            hi,lo,extra=rng.sample(range(16),3)
            ws=[hi,lo] + ([extra] if op==39 else [])
            r.poke(STREAM,struct.pack('>%dH'%len(ws),*ws)); r.w16(0x5103C,0)
            r.run(0x23AF0 if op==39 else 0x23CB2,a2=STREAM,a6=0x710000)
            d0=r.reg('d0')&0xffff; log=hook.read(r); n,v=hook.flags(r)
            S=[tuple(vals[3*k:3*k+3]) for k in range(8)]
            # expected
            inside=all(abs(c)<=S[1][2] for c in S[1][:2]) and S[1][2]>=max(S[1][0],S[1][1],-S[1][0],-S[1][1])
            ok=True
            if not inside:
                exp=[]; want=0xffff
            else:
                dx=w16(-cx-w16(px<<lod)); dz=w16(-cz-w16(pz<<lod))
                variantA=(dx>=0)==(dz>=0)            # sign of x and z of the camera relative to the object origin agree
                pen_hi = abs(dz)<=abs(dx)            # |dz|>|dx| keeps the low word (second word of the long)
                pen = hi if pen_hi else lo
                if op==39:
                    if variantA:
                        poly=[S[0],S[1],add(S[2],S[3],tuple(-c for c in S[1])),add(S[0],tuple(-2*c for c in S[1]),S[2],S[3])]
                    else:
                        poly=[S[2],S[3],add(S[3],S[0],tuple(-c for c in S[1])),add(S[2],S[0],tuple(-c for c in S[1]))]
                    exp=[(pen&0xffff,poly)]
                    if gate:
                        exp.append((extra,[S[0],add(S[0],S[2],tuple(-c for c in S[1])),add(S[0],tuple(-2*c for c in S[1]),S[2],S[3]),add(S[0],S[3],tuple(-c for c in S[1]))]))
                else:
                    exp=[(pen,[S[0],S[1],S[4]] if variantA else [S[0],S[2],S[3]])]
                want=None
            got=[(p,[tuple(x) for x in poly]) for k,p,poly in log if k=='clip']
            if inside:
                ok=(got==exp)
                cnt[(op,'A' if variantA else 'B',gate if op==39 else '-', 'hi' if pen_hi else 'lo')]=cnt.get((op,'A' if variantA else 'B',gate if op==39 else '-', 'hi' if pen_hi else 'lo'),0)+1
                ok=ok and d0 in (0,1) and n==0
            else:
                ok=(got==[] and d0==0xffff and n==1); cnt[(op,'reject')]=cnt.get((op,'reject'),0)+1
            if not ok:
                bad+=1
                if bad<4: print('DIFF',op,'got',got,'exp',exp,'d0',d0,'N',n)
    print('ops 39 and 40: %d constructed calls, %d differences; cases %s'%(2*N,bad,sorted(cnt.items(),key=str)))
    return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())

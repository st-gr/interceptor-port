"""Vertex-synthesis ops 41, 42, 43, 44, 59 against the rig: random 16-bit slot arrays, handler run directly.
   python3 synth_check.py SEED N"""
import sys, random, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
import symx
def w16(x): x&=0xffff; return x-0x10000 if x&0x8000 else x
HAND={41:0x24EFE,42:0x2539A,43:0x2524C,44:0x25342,59:0x25AEC}
def exprs(op):
    m=symx.M()
    if op==42: symx.run(0x253ea,0x2551c,m)
    if op==43: symx.run(0x2526a,0x25342,m)
    if op==44: symx.run(0x25360,0x2539a,m)
    if op==59: symx.run(0x25b3c,0x25cdc,m)
    return m
def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 200
    miss=inputs_missing()
    if miss: print('skipped (missing: %s)'%', '.join(miss)); return 0
    rng=random.Random(seed); r=Rig(); r.reset(); r.poke(0x34A28,b'\x4e\x75')
    ARR=0x4D43C; tot={}; bad={}
    for op in (41,42,43,44,59):
        tot[op]=0; bad[op]=0
        base_m=exprs(op) if op!=41 else None
        for it in range(N):
            vals=[rng.randint(-3000,3000) if rng.random()<.7 else rng.randint(-32768,32767) for _ in range(3*130)]
            v=rng.choice([0,6,12,18]) *1 ; v=6*rng.randint(0,3)
            r.poke(ARR,b''.join(struct.pack('>h',x) for x in vals)+bytes(0))
            r.poke(ARR+3*130*2,bytes(2000))
            s=rng.randint(-4,5)
            stream=struct.pack('>hh',v,s)
            r.poke(0x700000,stream); r.w16(0x4AA6E,0)
            before=r.peek(ARR,2*3*130+2000)
            r.run(HAND[op],a2=0x700000,a6=0x710000)
            after=r.peek(ARR,2*3*130+2000)
            bw=struct.unpack('>%dh'%(len(before)//2),before); aw=struct.unpack('>%dh'%(len(after)//2),after)
            woff=v//2   # word offset of base
            changed={i for i in range(len(bw)) if bw[i]!=aw[i]}
            exp={}
            env={'s%d.%d'%(k,a):bw[woff+3*k+a] for k in range(0,130) for a in range(3) if woff+3*k+a<len(bw)}
            if op==41:
                S=lambda k,a: bw[woff+3*k+a]
                for a in range(3):
                    dd=w16(S(2,a)-S(3,a)); dd=w16(dd<<s) if s>=0 else dd>>(-s)
                    exp[3*10+a]=w16(S(3,a)-dd)
                    exp[3*9+a]=w16(S(3,a)+S(2,a)-S(1,a))
                    exp[3*11+a]=w16(S(5,a)-S(6,a)+S(7,a))
                    exp[3*12+a]=w16(S(5,a)-2*S(6,a)+S(7,a)+S(8,a)-S(6,a)+S(6,a)) if False else w16(exp[3*11+a]+S(8,a)-S(6,a))
                    d3=w16(S(4,a)-S(6,a))
                    exp[3*13+a]=w16(S(5,a)+d3); exp[3*14+a]=w16(S(8,a)+d3); exp[3*15+a]=w16(S(7,a)+d3)
                    exp[3*16+a]=w16(exp[3*11+a]+d3); exp[3*17+a]=w16(exp[3*12+a]+d3)
            else:
                for off,e in base_m.mem.items():
                    exp[off//2]=symx.ev(e,env)
                S=lambda k,a: bw[woff+3*k+a]
                if op==42:
                    for a in range(3): exp[3*100+a]=w16(w16(w16(w16(S(13,a)+S(14,a))>>1)+S(10,a))>>1)
                if op==59:
                    for a in range(3): exp[3*110+a]=w16(w16(w16(w16(S(18,a)+S(19,a))>>1)+S(16,a))>>1)
            ok=(r.reg('d0')&0xffff)==0 and ((r.L.rig_get_reg(17)>>3)&1)==0
            for i in range(len(bw)):
                want=exp.get(i-woff, bw[i]) if i>=woff else bw[i]
                if aw[i]!=want and not (i>=woff and (i-woff) not in exp and False):
                    ok=False
            tot[op]+=1; nch=len(changed); minch=min(nch,globals().get("_m",999)); globals()["_m"]=minch
            if not ok:
                bad[op]+=1
                if bad[op]<3:
                    i=[i for i in range(len(bw)) if aw[i]!=(exp.get(i-woff,bw[i]) if i>=woff else bw[i])][:5]
                    print('DIFF op',op,'v',v,'s',s,'words',i,[(aw[j],exp.get(j-woff)) for j in i])
    print("min words changed per run",_m)
    for op in tot: print('op %d: %d random arrays, %d differences'%(op,tot[op],bad[op]))
    return 1 if any(bad.values()) else 0
if __name__ == '__main__':
    sys.exit(main())

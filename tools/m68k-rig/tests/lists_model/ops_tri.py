"""Ops 7 and 8 (bridge faces) against the rig: constructed block + slot array, handler run directly, polygon handed to the clipper
read back from 0x5103C.   python3 tri_check.py SEED N"""
import sys, random, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
def w16(x): x&=0xffff; return x-0x10000 if x&0x8000 else x
def s32(x): x&=0xffffffff; return x-(1<<32) if x&0x80000000 else x
ARR=0x4D43C; BLK=0x600000; STREAM=0x700000
def slots(bw,k): return tuple(bw[3*k:3*k+3])
def add(a,b): return tuple(w16(x+y) for x,y in zip(a,b))
def sub(a,b): return tuple(w16(x-y) for x,y in zip(a,b))
def setup(r,rng,tag):
    r.reset(); r.poke(0x34A28,b'\x4e\x75')
    vals=[rng.randint(-400,400) for _ in range(3*40)]
    for k in range(40):                      # depths mostly positive, some negative
        vals[3*k+2]=rng.randint(-100,1200) if rng.random()<.8 else rng.randint(-1200,-1)
    r.poke(ARR,b''.join(struct.pack('>h',x) for x in vals))
    nv=8; L=rng.randint(0,4); H=rng.randint(0,4)
    blk=struct.pack('>HhhBBBB',0,0,0,(H<<4)|L,0,nv,0)
    bv=[rng.randint(-3000,3000) for _ in range(3*nv)]
    blk+=b''.join(struct.pack('>h',x) for x in bv)
    r.poke(BLK,blk); r.w32(0x4AAEA,BLK)
    px=rng.randint(-200,200); pz=rng.randint(-200,200); lod=rng.randint(0,3); cx=rng.randint(-400,400); cz=rng.randint(-400,400)
    r.w16(0x4ABE2,px); r.w16(0x4ABE6,pz); r.w16(0x4AB70,lod); r.w16(0x4AB2A,cx); r.w16(0x4AB2E,cz)
    return vals,bv,L,px,pz,lod,cx,cz
def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 300
    if inputs_missing(): print('skipped (missing inputs)'); return 0
    rng=random.Random(seed); r=Rig(); bad=0; n=0; paths={}
    for it in range(N):
        for op in (7,8):
            vals,bv,L,px,pz,lod,cx,cz=setup(r,rng,op)
            colour=rng.randint(0,15); a=6*rng.randint(0,6); b=6*rng.randint(0,6); flag=rng.random()<.5
            c=6*rng.randint(0,8); f=6*rng.randint(0,8); base=6*rng.randint(0,20)
            if op==7: ws=[colour,a,b|(0x8000 if flag else 0),c,base]
            else: ws=[colour,c,a,b|(0x8000 if flag else 0),f,base]
            r.poke(STREAM,struct.pack('>%dH'%len(ws),*ws))
            r.w16(0x5103C,0); r.w16(0x5103E,0x77)
            r.run(0x24CB2 if op==7 else 0x24D9E,a2=STREAM,a6=0x710000)
            d0=r.reg('d0')&0xffff; sr=r.L.rig_get_reg(17)
            cnt=r.u16(0x5103E); poly=[tuple(struct.unpack('>3h',r.peek(0x51040+6*i,6))) for i in range(cnt)]
            # expected
            xa,za=bv[a//2],bv[a//2+2]; xb,zb=bv[b//2],bv[b//2+2]
            def blk(off,ax): return bv[off//2+ax]
            xa,za=blk(a,0),blk(a,2); xb,zb=blk(b,0),blk(b,2)
            D3=w16(w16(w16(xa>>L)+px)<<lod)+cx; D3=w16(D3); D4=w16(w16(w16(za>>L)+pz)<<lod); D4=w16(D4+cz)
            T=s32(D3*w16(xa-xb)+D4*w16(za-zb))
            S=lambda k: slots(vals,(base//6)+k) if True else None
            Sa=lambda off: tuple(vals[off//2:off//2+3])
            if op==7:
                first=Sa(c)
                pathB=(T>=0)==flag if False else None
                useB = (T>=0 and flag) or (T<0 and not flag)
                s0,s1,s2=S(0),S(1),S(2)
                exp=[first,s1,add(sub(s0,s1),s2)] if useB else [first,s0,s2]
            else:
                p=sub(Sa(f),sub(Sa(c+6),Sa(c)))
                useB=None
                s0,s1,s2,s3=S(0),S(1),S(2),S(3)
                e1=sub(s1,s0); e2=sub(s2,s1)
                # T>=0: flag -> path 9E else path 52 ; T<0: flag -> 52 else 9E
                p52 = (T>=0 and not flag) or (T<0 and flag)
                exp=[p,s3,add(add(s3,e1),e2)] if p52 else [p,add(s3,e1),add(s3,e2)]
                useB=p52
            zs=[v[2] for v in exp]
            behind=all(z<0 for z in zs)
            ok=(cnt==3 and [tuple(x) for x in poly]==exp)
            # expected return: 0 if all behind else clipper result on the expected polygon
            rr=Rig.run
            if ok:
                r.w16(0x5103C,0); 
                r.poke(0x51040,b''.join(struct.pack('>3h',*v) for v in exp)); r.w16(0x5103E,3)
                r.run(0x28B6A,a6=0x710000); clipd0=r.reg('d0')&0xffff
                want=0 if behind else clipd0
                ok = (d0==want)
                if not ok and bad<3: print('d0',d0,'clip',clipd0,'behind',behind)
            ok=ok and ((sr>>3)&1)==0 and d0 in (0,1)
            n+=1; paths[(op,useB)]=paths.get((op,useB),0)+1
            if not ok:
                bad+=1
                if bad<4: print('DIFF op',op,'got',poly,'exp',exp,'d0',d0,'T',T,'flag',flag)
    print('ops 7 and 8: %d constructed faces, %d differences; paths taken %s'%(n,bad,sorted(paths.items(),key=str)))
    return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())

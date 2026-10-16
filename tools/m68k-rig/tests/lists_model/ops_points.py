"""Point ops 1, 49, 58 and 62: projection, sprite choice, pen, return; direct handler runs with the sprite routines logged.
   python3 points_check.py SEED N"""
import sys, random, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
import handler_hook as hook, visibility as vis
def w16(x): x&=0xffff; return x-0x10000 if x&0x8000 else x
def tdiv(a,b): q=abs(a)//abs(b); return q if (a<0)==(b<0) else -q
ARR=0x4D43C; STREAM=0x700000; FR=0x710000
def project(x,y,z,maxrow):
    """0x33780 front end: None when rejected, else (screen x, screen y)"""
    if x>=z or y>=z or -x>=z or -y>=z or z<=0: return None
    sx=tdiv(x*160,z)+160; sx=0 if sx<0 else 0x13F if sx>=0x140 else sx
    sy=tdiv(y*90,z)+90; sy=0 if sy<0 else 0xB3 if sy>=0xB4 else sy
    X=0x13F-sx; Y=0xB3-sy+1
    if Y>maxrow: return None
    return (X,Y)
def facing(v0,v1,v2,W):
    k=(W>>7)&7
    e1=[w16((a-b)<<k) for a,b in zip(v1,v0)]; e2=[w16((a-b)<<k) for a,b in zip(v2,v0)]
    nx=w16((e1[1]*e2[2]-e1[2]*e2[1])>>8); ny=w16((e1[2]*e2[0]-e1[0]*e2[2])>>8); nz=w16((e1[0]*e2[1]-e1[1]*e2[0])>>8)
    return 0 if nx*v0[0]+ny*v0[1]+nz*v0[2]<0 else 1          # 1 = faces away
def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 400
    if inputs_missing(): print('skipped (missing inputs)'); return 0
    rng=random.Random(seed); r=Rig(); r.reset(); m=vis.Mem(r.peek(0,1<<20)); bad=0; cnt={}
    for it in range(N):
        for op in (1,58,49,62):
            r.reset(); r.poke(0x34A28,b'\x4e\x75'); hook.install(r)
            vals=[]
            for k in range(8):
                z=rng.randint(20,3000) if rng.random()<.9 else rng.randint(-300,3000); lim=max(z,1)
                vals+=[rng.randint(-lim,lim)*1,rng.randint(-lim,lim),z]
                if rng.random()<.1: vals[-3]=rng.randint(-2*lim,2*lim)
            r.poke(ARR,b''.join(struct.pack('>h',x) for x in vals))
            lod=rng.randint(0,6); sd=rng.choice((rng.randint(0,40),rng.randint(0,400),rng.randint(0,2000)))
            maxrow=rng.choice((0x7FFF,0xB3,143,166)); sv=rng.randint(0,3)
            r.w16(0x4AB70,lod); r.w16(FR-0x28,sd); r.w16(0x4AA3C,maxrow); r.w16(FR-8,sv); r.w16(0x5103C,0)
            pen=rng.randint(0,0xFFFF); v=6*rng.randint(0,7)
            S=[tuple(vals[3*k:3*k+3]) for k in range(8)]
            if op in (1,58): ws=[v,pen]; addr=0x23544 if op==1 else 0x23566
            elif op==49:
                W=rng.choice((0,0x80,0x100,0x180,0x200,0x300,0x380))|rng.randint(0,15); ws=[6*rng.randint(0,7),6*rng.randint(0,7),6*rng.randint(0,7),W]; addr=0x23666
            else:
                size=rng.choice((rng.randint(1,200),rng.randint(1,3000),rng.randint(1,30000),rng.randint(0x8000,0xFFFF))); ws=[v,pen,size]; addr=0x25750
            r.poke(STREAM,struct.pack('>%dH'%len(ws),*ws))
            r.run(addr,a2=STREAM,a6=FR)
            d0=r.reg('d0')&0xffff; log=hook.read(r); n,vf=hook.flags(r)
            sp=[x for x in log if x[0]=='sprite']
            # expected
            exp=[]; want=0
            if op==49:
                a,b,c,W=ws[0]//6,ws[1]//6,ws[2]//6,ws[3]
                if facing(S[a],S[b],S[c],W)==0: P=S[a]; pn=W
                else: P=None
            elif op==62:
                P=tuple(w16(c<<sv) for c in S[v//6]); pn=pen
            else: P=S[v//6]; pn=pen
            if P is not None:
                pr=project(*P,maxrow) if op!=62 else project(*P,maxrow)
                if op==62:
                    d=vis.fx_dist(m,abs(P[0]),abs(P[1]),abs(P[2]))&0xffff
                    d=w16(d)
                    # the dividend is the operand word sign-extended to 32 bits (divu.w), an overflowing quotient leaves the low word of the operand
                    D6=size if size<0x8000 else 0xFFFF0000|size
                    R=0x7F if d<=0 else ((D6//d) if D6//d<=0xFFFF else size)
                    R=w16(R); R=0x7F if R>0x7F else R
                if pr is not None:
                    if op==62: kind=4; exp=[('sprite',4,pr[0],pr[1],pn&0xffff,R)]
                    else:
                        if op==58: kid=2
                        elif (0x30>>lod)>=sd: kid=3
                        elif (0x50>>lod)>=sd: kid=2
                        else: kid=1
                        exp=[('sprite',kid,pr[0],pr[1],pn&0xffff)]
                    want=1
            got=[(s[0],s[1],s[2],s[3],s[4])+((s[5],) if op==62 else ()) for s in sp]
            nexp=1 if (P is not None and pr is None) else 0
            ok=(got==exp and d0==want and n==nexp)
            key=(op,'drawn' if want else 'none')+((exp[0][1],) if exp and op!=62 else ())
            cnt[key]=cnt.get(key,0)+1
            if not ok:
                bad+=1
                if bad<5: print('DIFF op',op,'got',got,'exp',exp,'d0',d0,'want',want,'vals',vals[:6],ws,lod,sd)
    print('ops 1, 49, 58, 62: %d constructed calls, %d differences'%(4*N,bad)); print(sorted(cnt.items(),key=str))
    return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())

"""Point sprites 0x340D4, 0x340EA, 0x3414E run for real on four plane buffers: pixels and colour.  python3 sprite_check.py SEED N"""
import sys, random, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 300
    if inputs_missing(): print('skipped (missing inputs)'); return 0
    rng=random.Random(seed); r=Rig(); bad={}; tot={}
    P=[0x600000+0x10000*i for i in range(4)]
    for it in range(N):
        for name,a in (('340D4',0x340D4),('340EA',0x340EA),('3414E',0x3414E)):
            r.reset()
            r.poke(0x700000,b''.join(struct.pack('>I',p) for p in P)); r.w32(0x4A76E,0x700000)
            for p in P: r.poke(p,b'\xff'*8000)
            x=rng.randint(0,319); y=rng.randint(1,170); pen=rng.randint(0,15); lim=rng.choice((0x7FFF,y+1,y,y-1))
            r.w16(0x4A79E,0xF); r.w16(0x4A7A0,0xFFFF); r.w16(0x4A7A2,0); r.w16(0x4AA0C,pen|rng.choice((0,0x10,0xFF00))); r.w16(0x4AA3C,lim)
            r.run(a,d0=x,d1=y,a6=0x710000)
            got={}
            for i,p in enumerate(P):
                data=r.peek(p,40*200)
                for row in range(max(0,y-1),min(200,y+3)):
                    for col in range(0,320):
                        got[(col,row)]=got.get((col,row),0)|((1<<(3-i)) if data[row*40+col//8]&(0x80>>(col%8)) else 0)
            use=name
            if name=='3414E' and y>=lim: use='340EA'
            if use=='340D4': pix={(x,y)}
            elif use=='340EA': pix={(x-1,y),(x,y)} if x else {(0,y),(319,y-1)}
            else:
                xs=(x,x+1) if x%16==0 else (x-1,x); pix={(a,b) for a in xs for b in (y,y+1)}
            got={k:v for k,v in got.items() if v!=15}
            exp={p:pen for p in pix if 0<=p[0]<320 and pen!=15}
            tot[name]=tot.get(name,0)+1
            if got!=exp:
                bad[name]=bad.get(name,0)+1
                if sum(bad.values())<4: print('DIFF',name,x,y,pen,lim,sorted(got.items())[:6],sorted(exp.items())[:6])
    print('sprites: %s random calls, differences %s'%(tot,bad or 0)); return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())

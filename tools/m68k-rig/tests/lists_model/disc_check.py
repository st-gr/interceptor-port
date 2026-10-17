"""Filled disc 0x33CAC (op 62 and the D7=-4 entry 0x33788): the table of half-widths and the blitter row spans, with the
blitter registers redirected to RAM and the four size writes logged.  python3 disc_check.py SEED N"""
import sys, random, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing
def w(*v): return b"".join(struct.pack(">H",x&0xFFFF) for x in v)
def l(v): return struct.pack(">I",v&0xFFFFFFFF)
LOG=0x8FF000; REC=0x900000; FAKE=0x7F0000
def table(R):
    """half-width pairs, row 0 at the top: the midpoint circle generator of 0x33CB2.."""
    if R==1: return [(-1,1)]
    n=2*R+1; t=[None]*n if False else []
    A=[]; B=[None]*(2*R)  # build as the code does with two cursors
    a=0; D2=R; D3=0; D4=3-2*R; top=[]; bot=[]
    rows_top=[]; rows_bot=[]
    while D3<D2:
        rows_top.append((-D3,D3)); rows_bot.append((-D2,D2))
        if D4<0: D4+=4*D3+6
        else: D4+=4*(D3-D2)+10; D2-=1
        D3+=1
    mid=[(-D3,D3)] if D3==D2 else []
    return rows_top,mid,rows_bot
def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 1; N=int(sys.argv[2]) if len(sys.argv)>2 else 60
    if inputs_missing(): print('skipped (missing inputs)'); return 0
    rng=random.Random(seed); r=Rig(); bad=0; rows=0
    P=[0x600000+8000*i for i in range(4)]
    for it in range(N):
        R=rng.choice((rng.randint(1,6),rng.randint(1,127))); cx=rng.randint(0,319); cy=rng.randint(40,120); pen=rng.randint(0,15)
        r.reset()
        r.poke(0x700000,b''.join(struct.pack('>I',p) for p in (0x600000,)*4)); r.w32(0x4A76E,0x700000)
        r.w32(0x54EBC,0x780000)                                 # the scratch table
        r.poke(0x2000, w(0x40E7,0x48E7,0x80C0, 0x2079)+l(LOG)+ # sr; d0/a0/a1 ; movea.l LOG,a0
            w(0x3147,0, 0x2142,2) if False else b'')
        # logger at 0x2000: appends (a0 of the caller = plane address, d7 = size, 0x40/0x44/0x46 of the fake blitter) 6 words
        code=w(0x40E7,0x2F09,0x2F08)                              # move sr,-(a7); a1; a0 (a0 is pushed last)
        code+=w(0x2279)+l(LOG)                                   # movea.l LOG.l,a1
        code+=w(0x22C8) if False else b''
        # record: [a0 high, a0 low, d7, $40(A2), $44(A2), $46(A2)] : use A2 as the fake blitter base held by the caller
        code+=w(0x32C8-0x32C8+0x32C8) if False else b''
        code=w(0x40E7,0x2F09)                                    # move sr,-(a7); move.l a1,-(a7)
        code+=w(0x2279)+l(LOG)                                   # movea.l LOG.l,a1
        code+=w(0x22C8)                                          # move.l a0,(a1)+
        code+=w(0x32C7)                                          # move.w d7,(a1)+
        code+=w(0x32EA,0x40, 0x32EA,0x44, 0x32EA,0x46)           # move.w $40(a2),(a1)+ ; $44 ; $46
        code+=w(0x23C9)+l(LOG)                                   # move.l a1,LOG.l
        code+=w(0x225F,0x44DF,0x4E75)
        r.poke(0x2000,code); r.w32(LOG,REC)
        # fake blitter base and the size-write sites -> jsr $2000.w
        r.poke(0x33DB0,w(0x45F9)+l(FAKE))
        for a in (0x33EB8,0x33EEA,0x33F1C,0x33F4E):
            assert r.peek(a,4)==w(0x3547,0x0058),hex(a)
            r.poke(a,w(0x4EB8,0x2000))
        r.w16(0x4AA0C,pen); r.w16(0x4AA3C,rng.choice((199,179,143))); r.w16(0x4AB70,0)
        r.poke(0x600000,bytes(32000))
        r.run(0x33CAC,d0=cx,d1=cy,d6=R,a6=0x710000)
        end=r.u32(LOG); n=(end-REC)//12
        got={}                                                      # (row, col) -> colour
        for i in range(n):
            a,d7,c0,m1,m2=struct.unpack('>IHHHH',r.peek(REC+14*i,12)) if False else (None,)*5
            raw=r.peek(REC+12*i,12); a=struct.unpack('>I',raw[:4])[0]; d7,c0,m1,m2=struct.unpack('>4H',raw[4:12])
            pl=[k for k in range(4) if P[k]<=a<P[k]+8000]
            if not pl: print("a",hex(a),d7,hex(c0),n,R,cx,cy); sys.exit(1)
            off=a-P[pl[0]]; row=off//40; col=(off%40)*8; nw=d7&0x3F
            for wd in range(nw):
                m=0xFFFF
                if wd==0: m&=m1
                if wd==nw-1: m&=m2
                for b in range(16):
                    if m&(0x8000>>b):
                        got.setdefault((row,col+16*wd+b),[0,0,0,0])
                        got[(row,col+16*wd+b)][pl[0]]= 1 if (c0&0xFF)==0xFA else 0
        # expected: the table as the code leaves it in memory, then R rows down and the same R rows mirrored up
        T=[None]*(2*R+2); a=0; b=R; D2=R; D3=0; D4=3-2*R
        if R==1: T[0]=(-1,1)
        else:
            while D3<D2:
                T[a]=(-D3,D3); T[b]=(-D2,D2)
                if D4<0: D4+=4*D3+6
                else: D4+=4*(D3-D2)+10; D2-=1; a+=1
                b-=1; D3+=1
            if D3==D2: T[a]=(-D3,D3)
        rowsx=T[:R]+list(reversed(T[:R]))
        maxrow=r.u16(0x4AA3C); y0=cy-(R-1); exp={}
        for k,(a_,b_) in enumerate(rowsx):
            y=y0+k
            if y<1 or y>maxrow: continue
            for x in range(max(0,cx+a_),min(319,cx+b_)+1): exp[(y,x)]=pen
        pairs=rowsx
        gotc={k:sum(v[i]<<i for i in range(4)) for k,v in got.items() if True}
        # planes P[0] is bitplane 3 of the pen (the pointer list is lowest-address-last): compare bit-reversed colour
        def rev(c): return int(format(c,'04b')[::-1],2)
        rows+=len(pairs)
        if '--dump' in sys.argv and R<=5:
            tb=r.peek(0x780000,2+4*(2*R+1)); print('R',R,'table',struct.unpack('>%dh'%(len(tb)//2),tb)); 
            for y in sorted({k[0] for k in gotc}): xs=sorted(k[1] for k in gotc if k[0]==y); print('  row',y-cy,'x',xs[0]-cx,xs[-1]-cx)
        if gotc!=exp:
            bad+=1
            if bad<4: print('DIFF R',R,'c',cx,cy,'pen',pen,'got',len(gotc),'exp',len(exp),sorted(set(gotc)^set(exp))[:6])
    print('disc: %d discs (radius 1 to 127), %d rows, %d differences'%(N,rows,bad)); return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())

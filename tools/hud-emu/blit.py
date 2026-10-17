import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import struct
from unicorn import *
CH=0x80000
def tr(p):
    p&=0xffffffff
    return p+CH if p<0x80000 else p
class Blitter:
    def __init__(s,uc):
        s.uc=uc; s.r={}; s.lines_done=0; s.log=[]
    def tr(s,p): return tr(p)
    def w(s,addr,val,size):
        off=addr-0xdff000
        if size==4:
            s.r[off]=(val>>16)&0xffff; s.r[off+2]=val&0xffff
            if off+2==0x58: s.go()
        else:
            s.r[off]=val&0xffff
            if off==0x58: s.go()
    def g(s,o): return s.r.get(o,0)
    def rd(s,a): return struct.unpack('>H',s.uc.mem_read(a,2))[0]
    def wr(s,a,v): s.uc.mem_write(a,struct.pack('>H',v&0xffff))
    def ptr(s,hi): return ((s.g(hi)<<16)|s.g(hi+2))&0xfffffffe
    def go(s):
        c0=s.g(0x40); c1=s.g(0x42); size=s.g(0x58)
        if c1&1: return s.line(c0,c1,size)
        h=size>>6 or 1024; w=size&63 or 64
        ash=c0>>12; bsh=c1>>12
        usea=c0>>11&1; useb=c0>>10&1; usec=c0>>9&1; used=c0>>8&1; lf=c0&0xff
        desc=c1>>1&1; ife=c1>>3&1; efe=c1>>4&1; fci=c1>>2&1
        apt=s.ptr(0x50); bpt=s.ptr(0x4c); cpt=s.ptr(0x48); dpt=s.ptr(0x54)
        amod=s.sg(s.g(0x64)); bmod=s.sg(s.g(0x62)); cmod=s.sg(s.g(0x60)); dmod=s.sg(s.g(0x66))
        afwm=s.g(0x44); alwm=s.g(0x46)
        adat=s.g(0x74); bdat=s.g(0x72); cdat=s.g(0x70)
        step=-2 if desc else 2
        s.log.append(('blit',hex(c0),hex(c1),h,w,hex(apt),hex(bpt),hex(cpt),hex(dpt)))
        for row in range(h):
            aold=0; bold=0; fc=fci
            for col in range(w):
                if usea:
                    a=s.rd(s.tr(apt)); apt+=step
                else: a=adat
                if col==0: a&=afwm
                if col==w-1: a&=alwm
                if useb:
                    b=s.rd(s.tr(bpt)); bpt+=step
                else: b=bdat
                if usec:
                    cdat=s.rd(s.tr(cpt)); cpt+=step
                if desc:
                    ash_a=(((a<<16)|aold)>>(16-ash))&0xffff if ash else a  # shift left in desc
                    bsh_b=(((b<<16)|bold)>>(16-bsh))&0xffff if bsh else b
                else:
                    ash_a=(((aold<<16)|a)>>ash)&0xffff
                    bsh_b=(((bold<<16)|b)>>bsh)&0xffff
                aold=a; bold=b
                d=0
                for i in range(16):
                    idx=((ash_a>>i&1)<<2)|((bsh_b>>i&1)<<1)|(cdat>>i&1)
                    if lf>>idx&1: d|=1<<i
                if ife or efe:
                    out=0
                    order=range(16) if True else None
                    for i in order:
                        inb=d>>i&1
                        o=inb
                        if ife: o=inb|fc
                        else: o=inb^fc
                        out|=o<<i
                        fc^=inb
                    d=out
                if used:
                    s.wr(s.tr(dpt),d); dpt+=step
            sg=-1 if desc else 1
            if usea: apt+=sg*amod
            if useb: bpt+=sg*bmod
            if usec: cpt+=sg*cmod
            if used: dpt+=sg*dmod
        s.r[0x58]=0
    def sg(s,v):
        v&=0xfffe
        return v-0x10000 if v&0x8000 else v
    def line(s,c0,c1,size):
        n=size>>6
        ash=c0>>12; bsh=c1>>12; lf=c0&0xff
        usec=c0>>9&1; used=c0>>8&1
        octant=(c1>>2)&7  # bit4 SUD,bit3 SUL, bit2 AUL
        sud=c1>>4&1; sul=c1>>3&1; aul=c1>>2&1
        sign=c1>>6&1; sing=c1>>1&1
        err=s.sg(s.ptr(0x50)&0xffff)
        amod=s.sg(s.g(0x64)); bmod=s.sg(s.g(0x62)); cmod=s.sg(s.g(0x60)); dmod=s.sg(s.g(0x66))
        cpt=s.ptr(0x48); dpt=s.ptr(0x54)
        bdat=s.g(0x72); adat=s.g(0x74)
        x=ash
        tex=bdat
        s.log.append(('line',n,hex(c0),hex(c1),hex(cpt)))
        onedot=False
        for i in range(n):
            apix=(0x8000>>x)&0xffff
            bbit=0xffff if (tex>>(15-((bsh+i)&15)))&1 else 0   # texture bit
            cd=s.rd(s.tr(cpt)) if usec else s.g(0x70)
            d=0
            for k in range(16):
                idx=((apix>>k&1)<<2)|((bbit>>k&1)<<1)|(cd>>k&1)
                if lf>>idx&1: d|=1<<k
            if used and not (sing and onedot): s.wr(s.tr(dpt),d)
            onedot=True
            # step
            if sign: err+=bmod; major_only=True
            else: err+=amod; major_only=False
            sign=1 if err<0 else 0
            # major
            if not sud: # vertical major (SUD=0)
                dy=-1 if aul else 1
                cpt+=dy*cmod; dpt+=dy*dmod; onedot=False
                if not major_only:
                    dx=-1 if sul else 1
                    x+=dx
                    if x>15: x=0; cpt+=2; dpt+=2
                    if x<0: x=15; cpt-=2; dpt-=2
            else:
                dx=-1 if aul else 1
                x+=dx
                if x>15: x=0; cpt+=2; dpt+=2
                if x<0: x=15; cpt-=2; dpt-=2
                if not major_only:
                    dy=-1 if sul else 1
                    cpt+=dy*cmod; dpt+=dy*dmod; onedot=False
        s.r[0x58]=0

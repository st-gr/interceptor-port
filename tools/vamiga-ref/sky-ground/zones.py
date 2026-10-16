"""Model of the sea-zone lookup 0x230F8 (sets V:4A953 through 0x230B8: table 0x23188; V:4A954 through 0x230D8: table 0x233B8)."""
import struct
import os
_P=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','..','extracted','interceptor.bin')
if not os.path.exists(_P):
    print("skipped: needs extracted/interceptor.bin")
    raise SystemExit(0)
d=open(_P,'rb').read()
B=0x10000
def w(a): return struct.unpack('>h',d[a-B:a-B+2])[0]
def u8(a): return d[a-B]
def l(a): return struct.unpack('>i',d[a-B:a-B+4])[0]
def lookup(base,cz,cx,alt,phase,octant):
    """model of 0x230F8: cz=V:4AA02, cx=V:4AA00, alt = -(V:4AB30) (long), phase=V:4A908, octant=V:4A90C"""
    a=base; idx=0
    while True:
        v=w(a); a+=2
        if v<0: return 0
        if v==cz: break
        idx+=1
    while w(a)>=0: a+=2
    a+=2
    # a points after terminator; offsets table at a + 2*idx
    off=w(a+2*idx); a=base+off
    idx=0
    while True:
        v=w(a); a+=2
        if v<0: return 0
        if v==cx: break
        idx+=1
    while w(a)>=0: a+=2
    a+=2
    off=w(a+2*idx); a=base+off
    while True:
        if w(a)<0: return 0
        flag=w(a); a+=2
        bound=l(a); a+=4
        if flag==0:
            if alt>bound: return 0
        else:
            if alt<bound: return 0
        d6=phase
        while True:
            p=u8(a); a+=1
            if p>=128:
                if a&1: a+=1
                break
            if p==d6:
                d6=octant
                hit=False
                while True:
                    o=u8(a); a+=1
                    if o>=128: break
                    if o==d6: hit=True; break
                if hit: return 1
            else:
                while u8(a)<128: a+=1
                a+=1

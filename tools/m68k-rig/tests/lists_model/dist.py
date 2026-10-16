"""Python version of the distance function 0x20884 (bit-exact against the rig, 3000 random inputs)."""
import struct
from model import *
T=[uw(0x208e8+2*i) for i in range(257)]
def s16(v): v&=0xffff; return v-0x10000 if v&0x8000 else v
def dist(x,y,z):
    """x,z words >=0, y long >=0 (low word compared) -> D1"""
    D2,D3=x&0xffff,y
    if s16(D3)>s16(D2): D2,D3=D3&0xffff,D2   # exg (D3 then gets old D2 word; ext.l)
    D3=s16(D3)
    if D3==0: ratio=0
    elif D2==0: ratio=0
    else: ratio=((D3<<8)&0xffffffff)//D2 & 0xffff
    T1=T[ratio]
    D2=(D2&0xffff)*T1
    D4=(s16(z)<<14)
    if D4>D2: D2,D4=D4,D2
    D2=D2>>14
    if (D2&0xffff)==0: ratio2=0
    else:
        q=(D4&0xffffffff)//(D2&0xffff) & 0xffff
        ratio2=((s16(q)>>6)&0xffff)
    D1=T[ratio2]*(D2&0xffff)>>14
    return min(D1,0x7fff)

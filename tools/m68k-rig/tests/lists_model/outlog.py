"""Output-routine logger (copy/extension of lists_model/primlog.py): 64-byte records, 32 words.
word0 kind (1 filler, 2 line, 3 sprite 340D4, 4 sprite 340EA, 5 sprite 3414E, 6 disc 33CAC), word1 pen V:4AA0C, word2 V:4A79E, word3 V:4A7A0,
word4 V:4A7A2, word5 V:4A7A4, word6 d0, word7 d1, word8 d2, word9 d3, word10 d6, words 11.. : filler count and up to 20 words of V:5043C."""
import struct, sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import primlog as P
from primlog import w, l
LOGPTR=0x8FF010; LOGBASE=0xA00000; RS=64
OUT={1:0x34A28,2:0x3455E,3:0x340D4,4:0x340EA,5:0x3414E,6:0x33CAC}
def stub(kind):
    c=w(0x40E7,0x2F08)
    c+=w(0x2079)+l(LOGPTR)
    c+=w(0x30BC,kind)
    for i,a in enumerate((0x4AA0C,0x4A79E,0x4A7A0,0x4A7A2,0x4A7A4)): c+=w(0x3179)+l(a)+w(2+2*i)
    for i,reg in enumerate((0,1,2,3)): c+=w(0x3140|reg,12+2*i)
    c+=w(0x3146,20)                      # move.w d6,20(a0)
    if kind==1:
        for i in range(21): c+=w(0x3179)+l(0x5043C+2*i)+w(22+2*i)
    c+=w(0x41E8,RS)+w(0x23C8)+l(LOGPTR)
    c+=w(0x205F,0x44DF,0x4E75)
    return c
def install(rig):
    addr=P.STUBS+0x2000
    for k,a in OUT.items():
        c=stub(k); rig.poke(addr,c); rig.poke(a,w(0x4EF9)+l(addr)); addr+=(len(c)+3)&~3
    rig.w32(LOGPTR,LOGBASE)
def read(rig):
    end=rig.u32(LOGPTR); n=(end-LOGBASE)//RS
    raw=rig.peek(LOGBASE,RS*n); rig.w32(LOGPTR,LOGBASE)
    return [struct.unpack('>32H',raw[RS*i:RS*i+RS]) for i in range(n)]

def clip_input_wrapper(rig, site):
    """record the clip input at V:5103C (shift, n, n triples; up to 6 vertices) before the clip call at the jsr site, kind 7"""
    addr=P.STUBS+0x3000
    c=w(0x40E7,0x2F08)+w(0x2079)+l(LOGPTR)+w(0x30BC,7)
    for i in range(20): c+=w(0x3179)+l(0x5103C+2*i)+w(2+2*i)
    c+=w(0x41E8,RS)+w(0x23C8)+l(LOGPTR)+w(0x205F,0x44DF)
    c+=w(0x4EF9)+l(0x28B6A)
    rig.poke(addr,c); rig.poke(site+2,l(addr))

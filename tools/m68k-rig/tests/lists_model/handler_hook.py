"""Direct-handler test harness: logs (pen, input polygon) at every call of the clipper 0x28B6A/0x28B6C from the handler region and
(id, d0, d1, pen, d6, d7) at every call of the sprite routines.  Rig RAM patched only."""
import struct, sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig
LOGPTR=0x8FF000; LOGBASE=0x900000; STUBS=0x800000
def w(*v): return b"".join(struct.pack(">H",x&0xFFFF) for x in v)
def l(v): return struct.pack(">I",v&0xFFFFFFFF)
def clip_wrapper(target):
    c=w(0x40E7,0x48E7,0x80C0)                       # move sr,-(a7); movem.l d0/a0/a1,-(a7)
    c+=w(0x2079)+l(LOGPTR)                           # movea.l LOGPTR.l,a0
    c+=w(0x30FC,1)                                   # move.w #1,(a0)+ kind
    c+=w(0x30F9)+l(0x4AA0C)                          # move.w pen,(a0)+  (word 1)
    c+=w(0x43F9)+l(0x5103E)                          # lea 5103E.l,a1
    c+=w(0x700C,0x30D9,0x51C8,0xFFFC)                # 13 words
    c+=w(0x23C8)+l(LOGPTR)                           # move.l a0,LOGPTR
    c+=w(0x4CDF,0x0301,0x44DF)                       # movem.l (a7)+,d0/a0/a1 ; move (a7)+,ccr
    c+=w(0x4EF9)+l(target)
    return c                                         # log record: kind, pen, count, 12 vertex words = 15 words (30 bytes)
def sprite_stub(sid):
    c=w(0x40E7,0x48E7,0xC080)                        # sr; d0/d1/a0
    c+=w(0x2079)+l(LOGPTR)
    c+=w(0x30BC,2, 0x317C,sid,2, 0x3140,4, 0x3141,6)  # kind 2, id, d0, d1
    c+=w(0x3179)+l(0x4AA0C)+w(8)                      # pen
    c+=w(0x3146,10, 0x3147,12)                        # d6, d7
    c+=w(0x41E8,16)+w(0x23C8)+l(LOGPTR)
    c+=w(0x4CDF,0x0103,0x44DF,0x4E75)                # movem d0/d1/a0 mask: d0 b0,d1 b1,a0 b8 -> 0x0103
    return c
def install(rig):
    addr=STUBS; wr={}
    for t in (0x28B6A,0x28B6C):
        c=clip_wrapper(t); rig.poke(addr,c); wr[t]=addr; addr+=(len(c)+3)&~3
    img=rig.peek(0x22C00,0x26000-0x22C00)
    for t,a in wr.items():
        pat=w(0x4EB9)+l(t); i=img.find(pat)
        while i>=0:
            if i%2==0: rig.poke(0x22C00+i+2,l(a))
            i=img.find(pat,i+2)
    for sid,t in ((1,0x340D4),(2,0x340EA),(3,0x3414E),(4,0x33CAC)):
        c=sprite_stub(sid); rig.poke(addr,c); rig.poke(t,w(0x4EF9)+l(addr)); addr+=(len(c)+3)&~3
    rig.w32(LOGPTR,LOGBASE)
def read(rig):
    end=rig.u32(LOGPTR); out=[]; a=LOGBASE
    while a<end:
        k=rig.u16(a)
        if k==1:
            raw=struct.unpack('>15h',rig.peek(a,30)); cnt=raw[2]
            out.append(('clip',raw[1]&0xffff,[tuple(raw[3+3*i:6+3*i]) for i in range(min(cnt,4))])); a+=30
        else:
            raw=struct.unpack('>8h',rig.peek(a,16)); out.append(('sprite',raw[1],raw[2],raw[3],raw[4]&0xffff,raw[5],raw[6])); a+=16
    rig.w32(LOGPTR,LOGBASE); return out
def flags(rig):
    sr=rig.L.rig_get_reg(17); return (sr>>3)&1, (sr>>1)&1

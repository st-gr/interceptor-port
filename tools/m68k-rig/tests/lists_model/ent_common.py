import sys, struct, random, collections
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, load_scene, run_builder, inputs_missing
import walk_check as W, primlog as P, visibility as vis
MODEL=0x25ECC
def player_desc(r, mid=4):
    """descriptor slot 0 as 0x1D56C writes it: the model record with the routine replaced by 0x21C44"""
    rec=list(struct.unpack('>5I', r.peek(MODEL+20*mid,20))); rec[0]=0x21C44
    r.poke(0x2600C, struct.pack('>5I',*rec))
def slot_desc(r, slot, mid):
    """a record's descriptor as 0x276D2 writes it (model record copied unchanged)"""
    r.poke(0x2600C+20*slot, r.peek(MODEL+20*mid,20))
def entity_entries(r):
    return [e for e in W.window(r,'C') if r.u8(e+1)&0x50]
def stub_prims(r):
    r.poke(W.STUB,b"\x4e\x75")

def one_entity_frame(r, rng, rec=0, mid=4, tag='air'):
    """random frame; queue C reduced to the single entity entry of record `rec` at the camera position; returns the entry address"""
    W.random_frame(r,rng,tag=tag,active=True,extra=0,camr=300)
    if rec==0: player_desc(r,mid)
    else: slot_desc(r,rec,mid)
    e=W.window(r,'C')[0]
    w0=(rec<<8)|0x10
    r.w16(e,w0); r.w32(e+2,0x2600C+20*rec)
    r.w16(e+6,0); r.w16(e+8,0); r.w16(e+10,0)
    r.w16(e+0x10,0); r.w16(e+0x14,0); r.w8(e+0x12,0); r.w8(e+0x13,0)
    r.w16(e+24,0xFFFF)
    R=0x4B23C+0x200*rec
    r.w8(R+1, r.u8(R+1)|0x40)
    cx=r.u32(0x4AB1A); cz=r.u32(0x4AB22); cy=r.s32(0x4AB1E)
    r.w32(R+0x14,(cx&0xFFFFF)); r.w32(R+0x1C,(cz&0xFFFFF)); r.w32(R+0x18,(-cy))
    return e

import ctypes
def trace(r, addr, lo=0, hi=1<<32, maxn=3000000, **regs):
    """step the rig from addr until the sentinel; returns the list of pcs in [lo,hi) (consecutive duplicates dropped)"""
    L=r.L
    L.m68k_get_reg.argtypes=[ctypes.c_void_p,ctypes.c_int]; L.m68k_get_reg.restype=ctypes.c_uint
    L.m68k_execute.argtypes=[ctypes.c_int]
    for i in range(16): L.rig_set_reg(i,0)
    for k,v in regs.items(): L.rig_set_reg(r.NAMES.index(k), v&0xFFFFFFFF)
    sp=0xE00000-0x100-4
    r.w32(sp,0x100)
    L.rig_set_reg(15,sp); L.rig_set_reg(16,addr)
    out=[]
    for n in range(maxn):
        L.m68k_execute(1)
        pc=L.m68k_get_reg(None,16)
        if pc==0x100: break
        if lo<=pc<hi: out.append(pc)
    return out

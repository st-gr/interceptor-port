import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from util import *
def base_panel(name='cockpit-ground'):
    e=Emu3(SNAP%name)
    for a in range(0x4a8f0,0x4a8ff): e.b(a,0)
    e.b(0x4a8ee,3); e.call(0x35238); e.call(0x3548a)   # clean panel + cowl
    return e
def run_routine(e,addr,setup=None,force=True):
    """run one routine on top of clean panel; return dict (x,y)->pen of changed pixels and glyph set"""
    import copy
    before=e.img()
    pl_save=[bytes(e.uc.mem_read(p,8000)) for p in e.pl[:4]]
    e.glyphs=set()
    if setup: setup(e)
    if force: e.b(0x4a8ef,3)
    err=e.call(addr)
    after=e.img()
    ch={(x,y):after[y][x] for y in range(200) for x in range(320) if after[y][x]!=before[y][x]}
    for p,b in zip(e.pl[:4],pl_save): e.uc.mem_write(p,b)
    return ch,set(e.glyphs),err
def bbox(ch):
    if not ch: return None
    xs=[p[0] for p in ch]; ys=[p[1] for p in ch]; return (min(xs),min(ys),max(xs),max(ys),len(ch))

def region(e,x0,y0,x1,y1):
    im=e.img(); ch='0123456789abcdef'
    return [''.join(ch[im[y][x]] for x in range(x0,x1+1)) for y in range(y0,y1+1)]
def show(e,x0,y0,x1,y1,title=''):
    print(title)
    for i,l in enumerate(region(e,x0,y0,x1,y1)): print('%3d %s'%(y0+i,l))

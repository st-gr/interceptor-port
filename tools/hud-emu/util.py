import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from emu3 import *
from frame import hud_frame
SNAP=ROOT+'/extracted/captures/%s.vasnap'
def fresh(name='cockpit-ground',clear=True):
    e=Emu3(SNAP%name)
    if clear:
        for p in e.pl[:4]: e.uc.mem_write(p,bytes(8000))
    return e
def pix(e):
    im=e.img(); return {(x,y):im[y][x] for y in range(200) for x in range(320) if im[y][x]}
def art(px,x0,y0,x1,y1):
    ch='.123456789abcdef'
    for y in range(y0,y1+1):
        print('%3d '%y+''.join((ch[px[(x,y)]-0] if (x,y) in px else ' ') for x in range(x0,x1+1)))

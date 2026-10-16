import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from util import *
from logload import load_log
from frame import hud_frame
from PIL import Image
P=pal()
T=load_log(ROOT+'/extracted/captures/logs/hud.txt')
def capimg(p): return Image.open(p).convert('RGB').crop((186,42,826,242)).resize((320,200),Image.NEAREST)
def setup(name, base='cockpit-ground', force_counters=None):
    e=Emu3(SNAP%base)
    for tag in ('rec0','s71'):
        a,b=T['%s_%s'%(tag,name)]
        e.uc.mem_write(e.R(a),b)
    tp=e.uc_l(e.R(0x4a76e))&0xffffffff
    e.pl=[e.uc_l(tp+4*k)&0xffffffff for k in range(5)]
    return e
def clusters(diff):
    seen=set(); comps=[]
    for p in sorted(diff):
        if p in seen: continue
        st=[p]; seen.add(p); comp=[p]
        while st:
            q=st.pop()
            for dx in range(-3,4):
                for dy in range(-3,4):
                    r=(q[0]+dx,q[1]+dy)
                    if r in diff and r not in seen: seen.add(r); st.append(r); comp.append(r)
        xs=[c[0] for c in comp]; ys=[c[1] for c in comp]
        comps.append((min(xs),min(ys),max(xs),max(ys),len(comp)))
    return comps
if __name__=='__main__':
    for name,fr in (('runway-idle','hud-runway-idle'),('level','hud-level'),('climb20','hud-climb20'),('bank30','hud-bank30'),('carrier-idle','hud-carrier-idle')):
        e=setup(name)
        cap=capimg(ROOT+'/extracted/captures/openers/%s.ppm'%fr)
        e.b(0x4a8ee,3); e.b(0x4a8ef,3)
        for n in range(3): hud_frame(e,n)
        im=e.img()
        diff=set((x,y) for y in range(145,200) for x in range(320) if cap.getpixel((x,y))!=P[im[y][x]])
        print(name,len(diff),clusters(diff))

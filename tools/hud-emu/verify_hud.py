import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from util import *
from logload import load_log
from PIL import Image
P=pal()
T=load_log(ROOT+'/extracted/captures/logs/hud.txt')
def capimg(p): return Image.open(p).convert('RGB').crop((186,42,826,242)).resize((320,200),Image.NEAREST)
def run(name, frame, panel=True):
    e=Emu3(SNAP%'cockpit-ground')
    base=e.img()
    for tag in ('rec0','s71'):
        a,b=T['%s_%s'%(tag,name)]
        e.uc.mem_write(e.R(a),b)
    cap=capimg(frame)
    tp=e.uc_l(e.R(0x4a76e))&0xffffffff
    e.pl=[e.uc_l(tp+4*k)&0xffffffff for k in range(5)]
    # HUD only
    for p in e.pl[:4]: e.uc.mem_write(p,bytes(8000))
    e.call(0x37d78)
    im=e.img()
    ch=[(x,y) for y in range(200) for x in range(320) if im[y][x]]
    ok=sum(1 for (x,y) in ch if cap.getpixel((x,y))==P[im[y][x]])
    return e,im,cap,ch,ok
if __name__=='__main__':
    for name,fr in (('runway-idle','hud-runway-idle'),('level','hud-level'),('climb20','hud-climb20'),('bank30','hud-bank30'),('carrier-idle','hud-carrier-idle')):
        e,im,cap,ch,ok=run(name,ROOT+'/extracted/captures/openers/%s.ppm'%fr)
        print(name,len(ch),ok)
        bad=[(p,im[p[1]][p[0]],cap.getpixel(p)) for p in ch if cap.getpixel(p)!=P[im[p[1]][p[0]]]]
        print(bad[:6])

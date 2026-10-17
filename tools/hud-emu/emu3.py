import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import struct,sys
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from unicorn import *
from unicorn.m68k_const import *
import snap
from snapmem import S, m as HUNKS
from blit import Blitter
IMG=open(ROOT+'/extracted/interceptor.bin','rb').read()
SENT=0xbf0000
CHAR={}
for _i in range(64):
    _o=0x426d0-0x10000+2*_i
    CHAR.setdefault(0x426d0+struct.unpack('>H',IMG[_o:_o+2])[0],chr(0x20+_i))
class Emu3:
    def __init__(s,snapfile):
        s.s=S(snapfile)
        uc=s.uc=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
        uc.mem_map(0,0x80000); uc.mem_map(0xc00000,0x80000); uc.mem_map(0xdff000,0x1000); uc.mem_map(0xbf0000,0x1000)
        uc.mem_write(0,s.s.d[snap.CHIP:snap.CHIP+0x80000])
        uc.mem_write(0xc00000,s.s.d[snap.CHIP+0x80000:snap.CHIP+0x100000])
        s.bl=Blitter(uc); s.bl.tr=lambda p:p&0xffffffff
        tp=s.s.l(0x4a76e)&0xffffffff
        s.pl=[s.uc_l(tp+4*k)&0xffffffff for k in range(5)]
        s.glyphs=set(); s.R=s.s.real
        s.stubs={s.R(0x58f20),s.R(0x58f78),s.R(0x58f30)}
        s.gl={s.R(0x37bc2),s.R(0x372ca)}
        s.hooks()
    def uc_l(s,a): return struct.unpack('>i',s.uc.mem_read(a,4))[0]
    def hooks(s):
        uc=s.uc
        def mw(uc_,access,addr,size,value,ud):
            if 0xdff000<=addr<0xdff100: s.bl.w(addr,value&((1<<(8*size))-1),size)
        uc.hook_add(UC_HOOK_MEM_WRITE,mw,begin=0xdff000,end=0xdff0ff)
        def code(uc_,a,size,ud):
            s.n+=1
            if s.n>s.maxi: uc.emu_stop(); s.err='maxi'; return
            if a==SENT: uc.emu_stop(); return
            if a in s.gl:
                d1=uc.reg_read(UC_M68K_REG_D1)&0xffffffff; d4=uc.reg_read(UC_M68K_REG_D4)&0xffffffff; d3=uc.reg_read(UC_M68K_REG_D3)&0xffff
                for k in range(5):
                    if s.pl[k]<=d1<s.pl[k]+0x2000:
                        off=d1-s.pl[k]
                        s.glyphs.add((off//40,(off%40)*8+(d3>>12),s.chr_of(d4)))
            w=struct.unpack('>H',uc.mem_read(a,2))[0]
            if w==0xc30a or w==0x830a:
                a1=uc.reg_read(UC_M68K_REG_A1)-1; a2=uc.reg_read(UC_M68K_REG_A2)-1
                uc.reg_write(UC_M68K_REG_A1,a1); uc.reg_write(UC_M68K_REG_A2,a2)
                src=uc.mem_read(a2,1)[0]; dst=uc.mem_read(a1,1)[0]
                X=s.bcdC if getattr(s,'bcdpc',-1)+2==a else 0
                if w==0xc30a:
                    res=(src&0xf)+(dst&0xf)+X
                    if res>9: res+=6
                    res+=(src&0xf0)+(dst&0xf0)
                    C=1 if res>0x99 else 0
                    if C: res-=0xa0
                else:
                    res=((dst&0xf)-(src&0xf)-X)&0xffffffff
                    if res>9: res=(res-6)&0xffffffff
                    res=(res+(dst&0xf0)-(src&0xf0))&0xffffffff
                    C=1 if res>0x99 else 0
                    if C: res=(res+0xa0)&0xffffffff
                res&=0xff
                uc.mem_write(a1,bytes([res]))
                s.bcdC=C; s.bcdpc=a
                uc.reg_write(UC_M68K_REG_PC,a+2)
                return
            if a in s.stubs:
                sp=uc.reg_read(UC_M68K_REG_A7); ret=struct.unpack('>I',uc.mem_read(sp,4))[0]
                uc.reg_write(UC_M68K_REG_A7,sp+4); uc.reg_write(UC_M68K_REG_PC,ret)
        uc.hook_add(UC_HOOK_CODE,code)
    def chr_of(s,real):
        flat=None
        for i,hh in enumerate(HUNKS):
            ra=s.s.h[i][0]
            if ra<=real<ra+hh['size']: flat=hh['addr']+real-ra
        return CHAR.get(flat,'?')
    # variable access by flat address
    def w(s,fa,v): s.uc.mem_write(s.R(fa),struct.pack('>h',v))
    def l(s,fa,v): s.uc.mem_write(s.R(fa),struct.pack('>i',v))
    def b(s,fa,v): s.uc.mem_write(s.R(fa),bytes([v&255]))
    def rw(s,fa): return struct.unpack('>h',s.uc.mem_read(s.R(fa),2))[0]
    def rb(s,fa): return s.uc.mem_read(s.R(fa),1)[0]
    def rl(s,fa): return struct.unpack('>i',s.uc.mem_read(s.R(fa),4))[0]
    def call(s,flat,maxi=3000000,regs=None):
        uc=s.uc; sp=0xbf0f00
        uc.mem_write(sp-4,struct.pack('>I',SENT)); sp-=4
        uc.reg_write(UC_M68K_REG_A7,sp)
        for r in range(8): uc.reg_write(UC_M68K_REG_D0+r,0)
        for r in range(7): uc.reg_write(UC_M68K_REG_A0+r,0)
        for k,v in (regs or {}).items(): uc.reg_write(k,v)   # optional register inputs, e.g. {UC_M68K_REG_D0: 5}
        s.n=0; s.maxi=maxi; s.err=None; s.bl.log=[]
        a=s.R(flat)
        try: uc.emu_start(a,SENT,count=maxi+10)
        except UcError as e: s.err=str(e)+' pc=%x'%uc.reg_read(UC_M68K_REG_PC)
        return s.err
    def img(s):
        P=[bytes(s.uc.mem_read(p,8000)) for p in s.pl[:4]]
        out=[[0]*320 for _ in range(200)]
        for y in range(200):
            for x in range(320):
                v=0
                for k in range(4): v|=((P[k][y*40+x//8]>>(7-x%8))&1)<<(3-k)
                out[y][x]=v
        return out
M4={0:0,1:0,2:0,3:28,4:43,5:56,6:72,7:89,8:106,9:123,10:141,11:159,12:177,13:197,14:216,15:255}
def pal():
    o=0x29fa4-0x10000; r=[]
    for i in range(16):
        w=struct.unpack('>H',IMG[o+2*i:o+2*i+2])[0]; r.append((M4[(w>>8)&15],M4[(w>>4)&15],M4[w&15]))
    return r
def to_png(img,path,scale=3):
    from PIL import Image
    p=pal(); im=Image.new('RGB',(320,200))
    for y in range(200):
        for x in range(320): im.putpixel((x,y),p[img[y][x]])
    if scale>1: im=im.resize((320*scale,200*scale),Image.NEAREST)
    im.save(path)
    return im

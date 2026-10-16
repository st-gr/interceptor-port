"""Independent Python model of the display-list scan (tables of the image only); see 04a."""
import struct,sys
from common import image
IMG=image()
def rb(a,n): return IMG[a-0x10000:a-0x10000+n]
def sb(a): return struct.unpack('>b',rb(a,1))[0]
def ub(a): return rb(a,1)[0]
def sw(a): return struct.unpack('>h',rb(a,2))[0]
def uw(a): return struct.unpack('>H',rb(a,2))[0]
def ul(a): return struct.unpack('>I',rb(a,4))[0]
TAB={'A':0x47374,'B':0x47474,'C':0x47574}
def cell_items(tier,row,key):
    base=TAB[tier]
    off=sw(base+2*row)
    if off<=0: return None
    a=base+off; n2=sw(a)
    if n2<0: return None
    n=n2//2
    keys=[sw(a+2+2*i) for i in range(n)]
    if key not in keys: return None
    i=keys.index(key)
    p=ul(a+2+2*n+4*i)
    items=[]
    while ub(p)!=0xff:
        b0=ub(p); b1=ub(p+1)
        if b1&0x80: raise Exception('default offset item')
        items.append((b0&0xf,b1&0x7f,sw(p+2),sw(p+4)))
        p+=6
    return items
def lod_table(): return rb(0x20e4a,240)
def build(tier,st):
    """st: dict with a908,a909,a90c,aa(key,row) pair, a83e, hi(bool: camera height > 200ft), cam2a,cam2e,cam30(for LOD); returns entries (id, x_long, z_long, lod)"""
    C = tier=='C'
    lowalt = (tier=='A') and (not st['hi'])
    if tier in 'AB': ak,ar,ph=st['aa00'],st['aa02'],st['a908']
    else: ak,ar,ph=st['aa04'],st['aa06'],st['a909']
    k=ub(0x4625c+ph)
    if lowalt: base=0x4717e
    elif st['a83e']: base=0x4633a
    else: base=0x4659c
    j=ub(0x4628c+ph)
    if st['a83e'] and not lowalt: widx=2*ub(0x4628c+j)
    elif not lowalt: widx=16*j+2*ub(0x4626c+8*k+st['a90c'])
    else:
        sl=ub(0x4629c+16*k+st['a909']); widx=32*j+2*sl
    off=sw(base+widx)
    p=base+off
    remap=0x462dc+24*k; slotmap=0x4629c+16*k
    groups=[]
    # pass1 list parse
    q=p; lst=[]
    while sb(q)>=0 and len(lst)<14:
        t=ub(q); n=ub(q+1); lst.append((t,[ub(q+2+i) for i in range(n)])); q+=2+n
    lim=0x7f if C else 0x1e
    out=[]
    cellx = (cellw:=None)
    for g,(t,layers) in enumerate(lst):
        tt=ub(remap+t); dx=sb(0x20674+2*tt); dz=sb(0x20674+2*tt+1)
        lx=st['aa04']+dx; lz=st['aa06']+dz
        lx=lx+128 if lx<0 else lx; lz=lz+128 if lz<0 else lz
        label=(((lx<<8)&0xffff)+(lz&0xff))&0xffff
        key=ak+dx; row=ar+dz
        key = lim if key<0 else (0 if key>lim else key)
        row = lim if row<0 else (0 if row>lim else row)
        items=cell_items(tier,row,key)
        slots={}
        if items:
            for (lay,i,dx_,dz_) in items:
                slots.setdefault(lay,[]).append((i,dx_,dz_))
        # pass2
        cx,cz = (sw((0x20732 if C else 0x2069e)+4*tt), sw((0x20732 if C else 0x2069e)+4*tt+2))
        px,pz = sw(0x206f2+4*st['a908']), sw(0x206f2+4*st['a908']+2)
        if C: cx+=px; cz+=pz
        cx*=4; cz*=4
        for lb in layers:
            s=ub(slotmap+lb)
            sx=sw((0x20786 if C else 0x206f2)+4*s)*4; sz=sw((0x20786 if C else 0x206f2)+4*s+2)*4
            for n_,(i,dx_,dz_) in enumerate(slots.get(s,[])[:16]):
                xl=(dx_ if C else dx_*4) - px*4 + sx + cx
                zl=(dz_ if C else dz_*4) - pz*4 + sz + cz
                out.append((i,xl,zl,n_+1,s,g,label))
    return out

"""Extension of model.py: the LOD rule and the tier C run sort (terrain items only, no entities)."""
from model import *
from dist import dist
LODT=lod_table()
def sw16(v):
    v&=0xffff; return v-0x10000 if v&0x8000 else v
def lod_of(x,z,cam):
    xc=sw16((abs(x+sw16(cam['cam2a'])))>>12)
    zc=sw16((abs(z+sw16(cam['cam2e'])))>>12)
    yc=sw16((-cam['cam30'])>>11)
    m=max(xc,zc,yc)
    k=(m&0xffff)>>1
    return LODT[min(k,0xef)]
def key_of(xw,zw,lod,cam):
    d1=sw16(cam['cam2a'])>>lod; d7=sw16(cam['cam2e'])>>lod; d6=cam['cam30']>>lod
    a=sw16(xw+d1); a=-a if a<0 else a
    b=d6; b=-b if b<0 else b
    c=sw16(zw+d7); c=-c if c<0 else c
    if b>=0x7fff0: return (0x7fff<<lod)&0xffff
    return (dist(sw16(a)>>4,b>>4,sw16(c)>>4)<<lod)&0xffff
def build2(tier,st,cam,lm=True):
    """returns entries (id, lod, xword, zword, slot) in queue order incl. the tier C sort"""
    ents=[]
    for (i,xl,zl,n_,s,g,label) in build(tier,st):
        lod=lod_of(xl,zl,cam)
        xw=(xl>>lod)&0xffff; zw=(zl>>lod)&0xffff
        ents.append((i,lod,xw,zw,(g,s),xl,zl,label))
    if tier=='C':
        out=[];i=0
        while i<len(ents):
            j=i
            while j<len(ents) and ents[j][4]==ents[i][4]: j+=1
            run=ents[i:j]
            if len(run)>=2:
                run=run[:22]
                keyed=[(key_of(e[2],e[3],e[1],cam),k,e) for k,e in enumerate(run)]
                keyed.sort(key=lambda t:(-sw16(t[0]),t[1]))
                run=[e for _,_,e in keyed]+ents[i+len(run):j]
            out+=run; i=j
        ents=out
        if lm:
            L=[dict(id=e[0],lod=e[1],x=sw16(e[2]),z=sw16(e[3]),label=e[7],src=e) for e in ents]
            L=landmark_pass(L,st['aa04'],st['aa06'],cam)
            ents=[d['src'] for d in L]
    return ents

# ---- the tier C landmark ordering pass (routine 0x21444), terrain entries only ----
def w16(v):
    v&=0xffff; return v-0x10000 if v&0x8000 else v
def s32(v):
    v&=0xffffffff; return v-(1<<32) if v&0x80000000 else v
def ref_ok(eid):
    d=0x2600c+20*eid
    M=ul(d+4)
    if s32(M)<=0: return False
    w0=uw(M)
    if w0&0x8000: leaf=w0
    elif w0&0x4000: leaf=uw(M+2)
    else: leaf=uw(M+4)
    if leaf==0xffff: return False
    return bool(ub(M+7+(leaf&0xfff))&0x10)
def cell_diff(a,b):   # (a-b) as 16-bit word, then << 16 >> 2
    return (w16(a-b)<<16)>>2
def loc(e,lod):
    """local position in the entry's own cell (u): X<<lod - (cell - player cell)*16384"""
    return None
def pos_local(e,pcx,pcz,lod):
    x=(e['x']<<lod)-cell_diff(e['label']>>8,pcx)
    z=(e['z']<<lod)-cell_diff(e['label']&0xff,pcz)
    return x,0<<lod,z
STATS={}
REFLOD=False
def landmark_pass(L,pcx,pcz,cam):
    n=len(L)
    if n<=1: return L
    ref=None
    for i in range(n-1,-1,-1):
        if ref_ok(L[i]['id']): ref=i; break
    if ref is None: return L
    R=L[ref]; cx=R['label']>>8; cz=R['label']&0xff
    pcx&=0xff; pcz&=0xff
    camx=-cam['cam2a']+cell_diff(pcx,cx); camz=-cam['cam2e']+cell_diff(pcz,cz)
    def pos(e):
        x,y,z=pos_local(e,pcx,pcz,e['lod'])
        return x+cell_diff(e['label']>>8,cx), y, z+cell_diff(e['label']&0xff,cz)
    rx,ry,rz=pos_local(R,pcx,pcz,R['lod'])
    # window start
    j=ref
    while j-1>=0:
        px_,py_,pz_=pos(L[j-1])
        if abs(px_-rx)>0xc00 or abs(py_-ry)>0xc00 or abs(pz_-rz)>0xc00: break
        j-=1
    start=j
    processed=[]
    k=start
    while k<n:
        e=L[k]
        if k==ref: processed.append((k,None)); k+=1; continue
        p=pos(e)
        if k>ref:
            if abs(p[0]-rx)>0xa00 or abs(p[1]-ry)>0xa00 or abs(p[2]-rz)>0xa00: break
        # plane test; ref position uses shift 0 (d1 low nibble is 0 at this point)
        lx,ly,lz=pos_local(R,pcx,pcz,R['lod'] if REFLOD else 0)
        a3=w16(lx); a5=w16(lz)
        tx=w16(p[0]); tz=w16(p[2]); ty=w16(p[1])
        C=ul(0x2600c+20*R['id']+0x10)
        cnt=sw(C)
        assert cnt>=0, 'bsp branch'
        pl=ul(C+6)
        mark=0xc000
        for q in range(cnt+1):
            a=pl+12*q
            Px,Py,Pz=sw(a),sw(a+2),sw(a+4); Nx,Ny,Nz=sw(a+6),sw(a+8),sw(a+10)
            d2=w16(-w16(w16(Px+a3)-tx)); d4=w16(-w16(w16(Pz+a5)-tz))
            side_t=s32(d2*Nx+ty*Ny+d4*Nz)
            c2=-(Px+a3-camx) if False else -( (w16(Px+a3)) - camx)
            c3=-cam['cam30']
            c4=-((w16(Pz+a5))-camz)
            side_c=s32(w16(c2)*Nx+w16(c3)*Ny+w16(c4)*Nz)
            if side_c>=0:
                if side_t>=0: mark=0x8000; break
        processed.append((k,mark)); k+=1
    first=[L[i] for i,m in processed if m==0xc000]
    refe=[L[i] for i,m in processed if m is None]
    back=[L[i] for i,m in processed if m==0x8000]
    STATS['fired']=STATS.get('fired',0)+1
    if len(processed)<=1: return L
    STATS['window>1']=STATS.get('window>1',0)+1
    if [L[i] for i,m in processed]!=first+refe+back: STATS['reordered']=STATS.get('reordered',0)+1
    return L[:start]+first+refe+back+L[start+len(processed):]

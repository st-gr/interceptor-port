"""Port-free model of the original's draw-result rule (walker + drawer) for terrain entries, from the image and the state bytes.

Written from the disassembly (note 04a section 8b).  The model reads a Mem (a copy of the flat memory: the game image at its load
addresses plus the state hunks) and never calls the port.
"""
import struct

class Unsupported(Exception): pass

def s8(v): v&=0xff; return v-0x100 if v&0x80 else v
def s16(v): v&=0xffff; return v-0x10000 if v&0x8000 else v
def u16(v): return v&0xffff
def s32(v): v&=0xffffffff; return v-(1<<32) if v&0x80000000 else v
def asr(v,n): return v>>n if n<64 else (-1 if v<0 else 0)
def asr16(v,n): return s16(s16(v)>>min(n,63))
def asl16(v,n): return s16(s16(v)<<min(n,63))
def abs16(v):
    v=s16(v); return s16(-v) if v<0 else v

class Mem:
    def __init__(s,b): s.b=bytearray(b)
    def u8(s,a): return s.b[a]
    def s8(s,a): return s8(s.b[a])
    def u16(s,a): return (s.b[a]<<8)|s.b[a+1]
    def s16(s,a): return s16(s.u16(a))
    def u32(s,a): return (s.u16(a)<<16)|s.u16(a+2)
    def s32(s,a): return s32(s.u32(a))
    def w8(s,a,v): s.b[a]=v&0xff
    def w16(s,a,v): s.b[a]=(v>>8)&0xff; s.b[a+1]=v&0xff
    def w32(s,a,v): s.w16(a,v>>16); s.w16(a+2,v)

def fx_dist(m,x,y,z):
    """0x20884: x,z words (non-negative as passed), y a long; returns D1 (the function also stores it at V:4ABF8)"""
    T=[m.u16(0x208e8+2*i) for i in range(257)]
    D2=x&0xffff; D3=y; D4=z
    if s16(D3)>s16(D2): D2,D3=D3&0xffff,D2
    D3=s16(D3)
    if D3==0 or D2==0: ratio=0
    else: ratio=(((D3<<8)&0xffffffff)//D2)&0xffff
    t=T[ratio]
    D2=(D2&0xffff)*t
    D4=s16(D4)<<14
    if D4>D2: D2,D4=D4,D2
    D2>>=14
    if (D2&0xffff)==0: r2=0
    else:
        q=((D4&0xffffffff)//(D2&0xffff))&0xffff
        r2=(s16(q)>>6)&0xffff
    D1=T[r2]*(D2&0xffff)>>14
    if s32(D1)>0x7fff: D1=(D1&~0xffff)|0x7fff
    return D1

def distkey(m,x,y,z,lod,use4abf4=False):
    """0x2082A: x,y,z the entry's position words (V:4ABE2/4ABE4/4ABE6), lod = V:4AB70.  Returns D1."""
    d1=asr16(m.s16(0x4ab2a),lod); d6=m.s32(0x4ab30)>>lod; d7=asr16(m.s16(0x4ab2e),lod)
    d2=abs16(s16(x)+d1)
    if use4abf4: d3=m.s32(0x4abf4)>>8          # entity entries (V:4A973 set by 0x1FFC6): the long V:4ABF4 replaces word + camera height
    else: d3=s16(y)+d6
    d3=abs(d3)
    if d3>=0x7fff0: return 0x7fff
    d4=abs16(s16(z)+d7)
    return fx_dist(m,s16(d2)>>4,d3>>4,s16(d4)>>4)&0xffff

def vnorm(m,d0,d5,d6,d7):
    """0x29C0E: scale (d5,d6,d7) to length d0; returns the three scaled components (as 16-bit words, sign extended)"""
    d0=s16(d0); neg=d0<0
    if d0==0: return (0,0,0)
    if d0<0: d0=s16(-d0)
    d2=abs16(d5); d3=abs16(d6); d4=abs16(d7)
    d1=fx_dist(m,d2,d3,d4)
    if (d1&0xffff)==0: return (0,0,0)
    d1=s32(d1)
    sh=8; l=d0
    while not (l>d1): l<<=2; sh+=2
    while True:
        l>>=2; sh-=2
        if sh>1:
            if l<=d1: break
            continue
        break
    l<<=2; sh+=2
    l=(l<<8)&0xffffffff
    q=l//(d1&0xffff)
    d0q = (q&0xffff) if q<=0xffff else (l&0xffff)     # divu overflow leaves D0 unchanged: its low word is used by muls
    mm=s16(d0q)
    r=[s32(mm*s16(v))>>sh for v in (d5,d6,d7)]
    if neg: r=[-v for v in r]
    return tuple(r)

# ---------------------------------------------------------------- the drawer, terrain path
PRETEST_T1=0x23ff4; PRETEST_T2=0x2400c

class Draw:
    """One call of a descriptor routine for a terrain entry.  Fields mirror the registers and state words of the original."""
    def __init__(s,m,tier,eid,lod,x,y,z,dist,desc):
        s.m=m; s.tier=tier; s.eid=eid; s.lod=lod
        s.wx,s.wy,s.wz=s16(x),s16(y),s16(z)             # V:4ABE2/4ABE4/4ABE6
        s.lx,s.ly,s.lz=s32(s16(x)<<8),s32(s16(y)<<8),s32(s16(z)<<8)   # V:4ABE8/4ABEC/4ABF0 (tiers A, B and C terrain)
        s.dist=dist&0xffff                                # V:4ABF8
        s.desc=desc
        s.exit=None; s.log=[]; s.verts={}; s.ops=[]
    # --- 0x21D0C
    def run(s,routine):
        m=s.m
        if routine==0x21c34:
            if m.s32(0x4ab1e)>=-0x280000: s.exit='cut34'; return 0
        elif routine not in (0x21c40,0x21d0c): raise Unsupported('routine %x'%routine)
        return s.drawer()
    def scaled(s):
        m=s.m
        d1=s.dist
        if m.u8(0x4a895)==0:
            z=m.s16(0x4aafa); d3=(z<<8)&0xffffffff
            q=d3//0x80
            d3q=(q&0xffff) if q<=0xffff else (d3&0xffff)
            d1=(d1*d3q)&0xffffffff
            d1=s32(d1)>>8
        return d1
    def drawer(s):
        m=s.m; lod=s.lod
        base=m.u32(s.desc+4); s.base=base
        s.rawdist=s16(s.dist)
        d1=s.scaled(); s.d1=d1; sd=s16(d1); s.sd=sd        # -$28(a6) = low word
        a2=m.u32(s.desc+8)
        # detail list
        while True:
            d0=s16(m.u16(a2)); a2+=2
            if d0<0: break
            t=asr16(d0&0x3fff,lod)
            if t>sd:                                   # fits
                if not (d0&0x4000): a2+=2
                d0=s16(m.u16(a2)); a2+=2
                break
            if d0&0x4000: a2+=4; continue
            j=s16(m.u16(a2))
            if j<0: s.exit='nolevel'; return 0
            a2=base+j
        leaf=d0&0xffff
        if leaf==0xffff: s.exit='nolevel'; return 0
        tierC=(s.tier=='C')
        if leaf&0x2000:
            if tierC: s.exit='subst'; return 1
        else:
            if m.s16(0x4aafa)>=0x80:
                if not tierC: s.exit='subst'; return 1
            elif not tierC:
                a3=0x5476a
                while True:
                    w=m.s16(a3)
                    if w<0: break
                    if ((w&0xffff)>>8)==s16(s.eid): s.exit='subst'; return 1
                    a3+=24
        s.noclip=(leaf&0x4000)!=0
        d7=0 if s.noclip else 1
        if d7==1 and m.u8(0x4a856)!=0 and (m.u16(0x4a992)&1)==0: s.exit='alt'; return 0
        s.d7=d7
        s.leaf=leaf; a1=base+(leaf&0xfff); s.blk=a1; s.stream=a2
        return s.block()
    def block(s):
        m=s.m; lod=s.lod; a1=s.blk
        s.partid=m.u16(a1)
        d4=m.s16(a1+2); d5=m.s16(a1+4); b6=m.u8(a1+6)
        H=(b6>>4)&15; L=b6&15
        flags=m.u8(a1+7); s.flags=flags
        cam=[m.s32(0x4ab1a),m.s32(0x4ab1e),m.s32(0x4ab22)]
        c=[v>>lod for v in cam]; s.c=c               # -$20..-$18
        s.cw=[s16(v>>8) for v in c]                  # -$86..-$82
        s.ncw=[s16(-v) for v in s.cw]                # -$26..-$22
        d5s=asr16(d5,lod)
        if d5s<s.rawdist: sv=L; e=0
        else:
            d4s=asr16(d4,lod)
            if d4s<s.rawdist: sv=H; e=L-H
            else: sv=0; e=L
        s.sv=sv+lod; s.e=e                           # -8(a6), -6(a6)
        s.L=L
        # pre-test
        if m.s32(0x4ab30)>-0x140 and m.u8(0x4a924)==0 and (flags&0x42)==0x42:
            if m.s16(0x4ab72)<=0 or (m.u8(0x4a975)&3)==0:
                s.pretest_ran=True
                if s.pretest(a1+10)<0: s.exit='pretest'; return 0
        # vertices
        if flags&1: raise Unsupported('attitude block')
        r=s.terrain_vertices(a1,flags)
        if r is not None: return r
        s.log.append('vertices')
        return s.interpret()
    def pretest(s,a3):
        m=s.m
        x0=m.s16(a3); z0=m.s16(a3+2); x1=m.s16(a3+4); z1=m.s16(a3+6)
        d5=s16(-(s16(x0-x1))); d7=s16(-(s16(z0-z1)))
        ex,ey,ez=vnorm(m,0x100,d5,0,d7)
        c5=s16(s.ncw[0]-s.wx); c7=s16(s.ncw[2]-s.wz)
        cx,cy,cz=vnorm(m,0x100,c5,0,c7)
        d2=s16(ex)*s16(cx)+s16(ey)*s16(cy)+s16(ez)*s16(cz)
        d2=s32(d2)
        if d2<0: d2=-d2
        d2=s32(d2)>>4
        tab=PRETEST_T1 if m.s32(0x4ab30)>-0x80 else PRETEST_T2
        idx=asr16(s.sd,4)
        if idx>10: idx=11
        d1=m.s16(tab+2*idx)
        return -1 if s16(d2)<d1 else 0

    # ---- vertex set-up of the terrain path (0x2235C ...)
    def mat(s):
        return [s.m.s16(0x4ac90+2*i) for i in range(9)]
    def xf3(s,M,x,y,z):
        return (s16(s32(M[0]*x+M[1]*y+M[2]*z)>>8), s16(s32(M[3]*x+M[4]*y+M[5]*z)>>8), s16(s32(M[6]*x+M[7]*y+M[8]*z)>>8))
    def fv_ok(s,v):
        """first-vertex pyramid test (0x223F0/0x222D0): depth positive, |x| and |y| at most 1.5 depth (word arithmetic)"""
        d4=v[2]
        if d4<=0: return False
        d4=s16(d4+(d4>>1))
        if v[0]>d4 or s16(-v[0])>d4 or v[1]>d4 or s16(-v[1])>d4: return False
        return True
    def terrain_vertices(s,a1,flags):
        m=s.m; M=s.mat(); s.M=M
        n=s16(s8(m.u8(a1+8))); a=a1+10
        c=s.c
        d0=s32(c[0]+s.lx); d1=s32(c[2]+s.lz); d2=s32(c[1]+s.ly)
        sh=8-s.e
        px=s16(d0>>sh); pz=s16(d1>>sh); py=s16(d2>>sh)
        s.px,s.py,s.pz=px,py,pz
        verts=s.verts; sv=s.sv
        cnt=n-1
        if not (flags&2):
            slot=0
            while True:
                x=s.m.s16(a); y=s.m.s16(a+2); z=s.m.s16(a+4); a+=6
                v=s.xf3(M,s16(asr16(x,sv)+px),s16(asr16(y,sv)+py),s16(asr16(z,sv)+pz))
                verts[slot]=v
                if slot==0 and s.d7 and not s.fv_ok(v): s.exit='fv'; return 0
                slot+=1
                if slot==1:
                    if cnt<=0: break
                else:
                    cnt-=1
                    if cnt<=0: break
            s.nverts=slot
            if flags&8: return s.strips(a,px,py,pz,slot)
            return None
        # flat block
        if s.m.s32(0x4ab30)>=-0x20:
            lim=0x170 if (flags&4) else 0x60
            if asr16(lim,s.lod)<s.sd: s.exit='flatcull'; return 0
        yy=py
        cx_=s16(s32(yy*M[1])>>8); cy_=s16(s32(yy*M[4])>>8); cz_=s16(s32(yy*M[7])>>8)
        s.fc=(cx_,cy_,cz_)
        slot=0
        while True:
            x=asr16(s.m.s16(a),sv)+px; z=asr16(s.m.s16(a+2),sv)+pz; a+=4
            x=s16(x); z=s16(z)
            v=(s16(s16(s32(M[0]*x+M[2]*z)>>8)+cx_), s16(s16(s32(M[3]*x+M[5]*z)>>8)+cy_), s16(s16(s32(M[6]*x+M[8]*z)>>8)+cz_))
            verts[slot]=v
            if slot==0 and s.d7 and not s.fv_ok(v): s.exit='fv'; return 0
            slot+=1
            if slot==1:
                if cnt<=0: s.nverts=slot; return None
            else:
                cnt-=1
                if cnt<=0: break
        s.nverts=slot
        if flags&4: return s.section2(a,slot)
        return None
    def put(s,off,v): s.verts[off//6]=v
    def xf3_long(s,M,x,y,z):
        return (M[0]*x+M[1]*y+M[2]*z, M[3]*x+M[4]*y+M[5]*z, M[6]*x+M[7]*y+M[8]*z)
    def strips(s,a,px,py,pz,nslots):
        """flag bit 3 (3D): chains of points with division counts (0x22470 ...); a = block address of the first chain word"""
        m=s.m; M=s.M; sv=s.sv
        a3=nslots*6
        def pt():
            nonlocal a
            x=m.s16(a); y=m.s16(a+2); z=m.s16(a+4); a+=6
            return s.xf3_long(M,s16(asr16(x,sv)+px),s16(asr16(y,sv)+py),s16(asr16(z,sv)+pz))
        saved=0
        while True:
            C=m.s16(a); a+=2
            if C<0: break
            step=12
            A=pt()
            s.put(a3,tuple(s16(v>>8) for v in A)); a3+=12     # point A, skip the odd slot
            cnt=C
            while True:
                while True:                                     # 224ee: forward / backward point loop
                    B=pt()
                    n=m.s16(a); a+=2
                    if n>1:
                        d=[v>>4 for v in A]; e=[v>>4 for v in B]
                        diff=[s32(e[i]-d[i]) for i in range(3)]
                        qs=[]
                        for v in diff:
                            # DIVS.W: 32-bit by 16-bit; on overflow the register is unchanged (its low word then is used by EXT.L)
                            q=int(abs(v)/abs(n)) * (1 if (v<0)==(n<0) else -1) if n!=0 else 0
                            qs.append(q if -32768<=q<=32767 else s16(v))
                        for k in range(n-1):
                            d=[d[i]+qs[i] for i in range(3)]
                            s.put(a3,tuple(s16(d[i]>>4) for i in range(3))); a3+=step
                    A=B
                    s.put(a3,tuple(s16(v>>8) for v in A)); a3+=step
                    cnt-=1
                    if cnt<=0: break
                if step<0: break
                step=-12; saved=a3
                D=m.s16(a); a+=2
                if D==0: break
                if D<0: a3=saved-6; D&=0x7fff
                else: a3=saved-18
                cnt=D
            nxt=m.s16(a)
            if nxt<=0: break
            a3=saved
        s.nverts=max(s.verts)+1 if s.verts else 0
        return None
    def section2(s,a,nslots):
        """flag bit 2 (flat): groups of extruded points (0x22BDE ...)"""
        m=s.m; M=s.M; sv=s.sv
        a3=nslots*6
        cx_,cy_,cz_=s.fc; px,pz=s.px,s.pz
        def fpt():
            nonlocal a
            x=asr16(m.s16(a),sv)+px if sv else m.s16(a)+px
            z=asr16(m.s16(a+2),sv)+pz if sv else m.s16(a+2)+pz
            a+=4
            x=s16(x); z=s16(z)
            return (s16(s16(s32(M[0]*x+M[2]*z)>>8)+cx_), s16(s16(s32(M[3]*x+M[5]*z)>>8)+cy_), s16(s16(s32(M[6]*x+M[8]*z)>>8)+cz_))
        while True:
            cnt=m.s16(a); a+=2
            if cnt<0: break
            p2=s.verts[a3//6-2]; p1=s.verts[a3//6-1]
            delta=tuple(s16(p1[i]-p2[i]) for i in range(3))
            while True:
                v=fpt(); s.put(a3,v); a3+=6
                s.put(a3,tuple(s16(v[i]+delta[i]) for i in range(3))); a3+=6
                cnt-=1
                if cnt<=0: break
            w=m.s16(a); a+=2
            if w<0: break
            for _ in range(2):
                v=fpt(); s.put(a3,v); a3+=6
        s.nverts=max(s.verts)+1
        return None

    # ---- tree walk and program interpreter (0x225F0 ...)
    def interpret(s):
        m=s.m; base=s.base
        s.acc=0                      # -$7C(a6): OR of the handler results
        a5=s.stream
        stop=0                       # -$66(a6)
        guard=0
        while True:
            guard+=1
            if guard>5000: raise Unsupported('loop')
            if stop: s.exit='end'; return s.acc
            d0=s16(m.u16(a5)); a5+=2
            if d0>=0:
                d7=m.u16(a5); a5+=2
                if (d7&0xfc00)==0xfc00:
                    a5=base+m.s16(a5); continue
                if (d7&0xc00)!=0:
                    z=s.node_axis(d0,d7)
                elif d7&0x2000:
                    p=[m.s16(a5+2*i) for i in range(6)]; a5+=12
                    z=s.node_point(p)
                else:
                    offs=[d0,m.s16(a5),m.s16(a5+2)]; a5+=4
                    z=s.node_3v(offs,d7)
                if z: a5=base+m.s16(a5)
                else: a5=base+m.s16(a5+2)
                continue
            # draw item
            if d0==-1: s.exit='ffff'; return 0
            bit13=(d0&0x2000)!=0
            if bit13:
                if d0&0x1000: prog=m.u32(a5); a5+=4
                else: prog=base+(d0&0xfff)
                s.run_program(prog)
                stop=1                  # -$6C: the stream ends after this item
                continue
            if d0&0x4000: stop=1
            if d0&0x1000: lst=m.u32(a5); a5+=4
            else: lst=base+(d0&0xfff)
            while True:
                w=m.u16(lst); lst+=2
                last=(w&0x8000)!=0
                if w&0x1000: prog=m.u32(lst); lst+=4
                else: prog=base+(w&0xfff)
                s.run_program(prog)
                if last: break
    def node_3v(s,offs,flags):
        """three-vertex node: True selects the first child"""
        m=s.m
        v=[s.vert(o) for o in offs]
        return s.plane_test(v,flags)==0
    def vert(s,off):
        return s.verts[off//6]
    def plane_test(s,v,flags):
        """0x22ACC: 1 when the polygon faces away (dot >= 0), 0 when it faces the viewer"""
        (x0,y0,z0),(x1,y1,z1),(x2,y2,z2)=v
        e1=[s16(x1-x0),s16(y1-y0),s16(z1-z0)]
        e2=[s16(x2-x0),s16(y2-y0),s16(z2-z0)]
        k=(flags>>7)&7
        if k: e1=[asl16(t,k) for t in e1]; e2=[asl16(t,k) for t in e2]
        nx=s32(e1[1]*e2[2]-e1[2]*e2[1])>>8
        ny=s32(e1[2]*e2[0]-e2[2]*e1[0])>>8
        nz=s32(e1[0]*e2[1]-e1[1]*e2[0])>>8
        t=s32(s16(nx)*x0+s16(ny)*y0+s16(nz)*z0)
        return 0 if t<0 else 1
    def node_point(s,p):
        """0x22A94 with the six words of the node: point and normal"""
        m=s.m; lod=s.lod
        d0=asr16(p[0],lod)+s.wx; d1=asr16(p[1],lod); d2=asr16(p[2],lod)+s.wz
        d0=s16(d0-s.ncw[0]); d1=s16(d1-s.ncw[1]); d2=s16(d2-s.ncw[2])
        t=s32(d0*p[3]+d1*p[4]+d2*p[5])
        return t<0           # negative: d7 = 0 -> first child
    def node_axis(s,d0,flags):
        """0x22B3A: True selects the first child"""
        m=s.m; a0=s.blk; kind=(flags&0xc00)>>10
        L=m.u8(a0+6)&0xf
        if kind==1:
            d2=asr16(m.s16(a0+d0+0xc),L); d7=-m.s32(0x4ab30)
            less = d7 < d2
        elif kind==2:
            d2=asr16(m.s16(a0+d0+0xa),L); d7=s16(-m.s16(0x4ab2a))
            d6=asl16(m.s16(0x4abe2) if False else s.wx,s.lod)
            d2=s16(d2+d6); less = d7 < d2
        else:
            d2=asr16(m.s16(a0+d0+0xe),L); d7=s16(-m.s16(0x4ab2e))
            d6=asl16(s.wz,s.lod)
            d2=s16(d2+d6); less = d7 < d2
        if less:
            return bool(flags&0x1000)     # bit 12 set: clr d7 -> Z -> first child
        return not (flags&0x1000)
    def run_program(s,a2):
        """0x227FE: items until one with bit 14; a negative handler result abandons the program"""
        m=s.m; lod=s.lod
        sd=s.sd
        while True:
            d0=s16(m.u16(a2)); a2+=2
            if d0<0:
                if d0==-1: raise Unsupported('ffff op')
                last=(d0&0x4000)!=0
                op=d0&0x3fff
            else:
                if d0!=0x7fff:
                    g1=asr16(d0,lod)
                    if g1<sd:                       # far case
                        g2=s16(m.u16(a2)); a2+=2
                        if g2<0: return
                        if asr16(g2,lod)<sd: return
                    else:
                        g2=s16(m.u16(a2)); a2+=2
                        if g2>=0: return
                n=s16(m.u16(a2)); a2+=2
                if n>0:
                    off=s16(m.u16(a2)); a2+=2
                    s.extra_vertices(n,off)
                w=m.u16(a2); a2+=2
                last=(w&0x4000)!=0
                op=w&0x3fff
            r,a2=s.op(op,a2)
            if r is not None and r<0: return
            if r: s.acc|=r
            if last: return
    def extra_vertices(s,n,off):
        raise Unsupported('extra vertices')
    def op(s,op,a2):
        raise Unsupported('op %d'%(op//4))

# ---------------------------------------------------------------- the walkers
class EntityDraw:
    """Stand-in for the drawer of an entity or secondary entry: the drawer is not modelled, only the walker fields are."""
    exit='entity (drawer not modelled)'

def entity_position(m,w0,x,z):
    """0x1FFC6 / 0x1FFB4: the entry's words plus the record's fine offsets; returns the words (x, y, z) and stores V:4ABF4 and V:4A973"""
    lod=w0&15; idx=(w0>>8)&0xff
    R=(0x4d23c+0x20*idx) if (w0&0x40) else (0x4b23c+0x200*idx)
    xl=(s16(x)<<8)+((m.u32(R+0x14)&0xfffff)>>lod)
    zl=(s16(z)<<8)+((m.u32(R+0x1c)&0xfffff)>>lod)
    yl=m.s32(R+0x18)>>lod
    m.w32(0x4abf4,((m.s32(R+0x18)+m.s32(0x4ab1e))>>lod))
    m.w8(0x4a973,1)
    return s16(xl>>8),s16(yl>>8),s16(zl>>8)

def refresh_due(m,dist_word,lod,eid):
    """the refresh schedule of step 4: stored distance shifted left by the LOD against 0x400 and 0x100, then the frame counter V:4A992"""
    s=s32(s16(dist_word)<<lod)
    if s>=0x400: return False
    if s<0x100: return True
    f=m.u16(0x4a992)&3
    return f==0 if (eid&1)==0 else f==2

def walk_queue(m,tier,log=None):
    """Walk one queue like 0x1FA24 / 0x1FA36 (tiers A, B) or the queue C part of 0x1FBCC.
    Updates the entry fields (+0x10, +0x12, +0x13, +0x14) in m and returns a list of (entry address, result or None, Draw).
    Entity and secondary entries of queue C get the refresh from the record and the counter, no drawer result (result None)."""
    out=[]
    if tier=='A': q=0x53a4a; off=m.u16(0x4aa64)
    elif tier=='B': q=0x540da; off=m.u16(0x4aa66)
    else: q=0x5476a; off=m.u16(0x4aa68)
    tierC=(tier=='C')
    ea=q+off
    if tierC:
        m.w8(0x4a91c,1); m.w16(0x4ab72,0); m.w8(0x4a975,0)
    else:
        m.w8(0x4a91c,0)
    while True:
        w0=m.u16(ea)
        if w0==0xffff: break
        lod=w0&15; eid=(w0>>8)&0xff
        if eid&0x80: eid-=256
        m.w8(0x4a913,w0&0xff)
        desc=m.u32(ea+2)
        if m.s16(desc)<0: ea+=24; continue
        routine=m.u32(desc)
        if tierC:
            if routine==0x21c40 and m.s32(0x4ab1e)<-0x400000: ea+=24; continue
        else:
            if routine==0x21c40 and m.s32(0x4ab1e)<-0x380000: ea+=24; continue
        x=m.s16(ea+6); y=m.s16(ea+8); z=m.s16(ea+10)
        m.w8(0x4a973,0)
        entity=tierC and (w0&0x50)!=0
        if entity: x,y,z=entity_position(m,w0,x,z)
        if routine==0x21c34 and not tierC:
            # queues A and B: the walker jumps from the routine test straight to the draw counter (no refresh, no countdown)
            pass
        else:
            if refresh_due(m,m.s16(ea+0x10),lod,eid):
                m.w16(ea+0x10,distkey(m,x,y,z,lod,use4abf4=entity))
            if not entity:
                prev=m.s16(ea+0x14); m.w16(0x4ab72,prev)
                if prev<0:
                    c=s8(m.u8(ea+0x12)-1); m.w8(ea+0x12,c)
                    if c>=0: ea+=24; continue
                    m.w8(ea+0x12,m.u8(0x4a974))
        c=(m.u8(ea+0x13)-1)&0xff; m.w8(ea+0x13,c); m.w8(0x4a975,c)
        dist=m.u16(ea+0x10); m.w16(0x4abf8,dist)
        m.w16(0x4aa6c,eid&0xffff)
        if entity:
            out.append((ea,None,EntityDraw())); ea+=24; continue
        d=Draw(m,tier,eid,lod,x,y,z,dist,desc)
        try:
            r=d.run(routine)
        except Unsupported as e:
            d.exit='unsupported:'+str(e); r=None
        res=None
        if r is not None:
            res=s16(r) if s16(r)>0 else -1
            m.w16(ea+0x14,res)
        out.append((ea,res,d))
        ea+=24
    return out

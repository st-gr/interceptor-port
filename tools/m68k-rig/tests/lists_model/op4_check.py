"""Op 4 (0x238C6) gate and face loop against the rig.   python3 op4_check.py SEED N   (skips without extracted/ inputs)

The two heights are separate inputs: the camera height V:4AB1E (long, negative above the ground; read by steps 2 to 7 of the gate) and the
altitude long of the record at +0x18 (read only by the compare with the first distance word).  The record altitude is the camera height plus a
small offset (0, +-1, +-2, +-0x40; a larger one moves the entity out of the view before the handler is reached: the walker refreshes the entity
position from the record), so that the two straddle every boundary and a handler that read the wrong one is caught.  The face loop is also run
with face distance words that are not in descending order: a failing face ends the list (a handler that skipped it would draw later faces)."""
from ent_common import *
def gate_pred(b4,band,own,cls,h,off4e,ext,lod):
    D0=-h
    if (b4&0xC0) not in (0,0xC0): return 'closed1'
    if b4&0x40:
        D7=(-off4e)<<8
        if D0>=D7: return 'closed1'
        D0=(D0-D7)>>lod
    if D0< -0x100000: return 'minus1'
    if ext or band>=3: return 'open'
    if own:
        if band==0: return 'closed1'
        D2=-0x2000
    else:
        if cls==0x30 or D0>=-0x10000 or cls==0x14: return 'open'
        if band<=1: return 'closed1'
        D2=-0xA0000
    return 'closed1' if D0<D2 else 'open'
FACES=(0xa00,0xa00,0x800,0x800)       # the first distance words of the F/A-18 program (model 4, near body)
_A2=[]
def face_list():
    """address of the face list of the near body (the argument address of op 4 in the player entry), found once by a logged run"""
    if not _A2:
        r=Rig(); rng=random.Random(1)
        e=one_entity_frame(r,rng,rec=5,mid=4)
        r.poke(0x2600C+20*5, struct.pack(">5I",0x21C44,*struct.unpack(">4I",r.peek(MODEL+20*4+4,16))))
        P.install(r,pre=True)
        R=0x4B23C+0x200*5
        r.w8(R+4,0); r.w8(R+0x62,0x11); r.w8(0x4A923,3); r.w16(0x4A994,5); r.w32(R+0x18,0); r.w32(0x4AB1E,0); r.w32(0x4AB30,0)
        r.w16(e,(5<<8)|0x10); r.w16(e+0x10,0)
        P.clear_log(r); trace(r,0x1FBCC,0x238C0,0x23B00,a6=0x70000); lg=P.read_log(r)
        _A2.append([(x[1]<<16)|x[2] for x in lg if x[0]==7 and x[3]==4][0])
    return _A2[0]
def run(b4,band,own,cls,h,off4e,ext,lod,v10,ha=None,words=None):
    ha=h if ha is None else ha
    if words: face_list()          # the logged run needs its own rig instance: do it before this one is set up
    r=Rig(); rng=random.Random(1)
    e=one_entity_frame(r,rng,rec=5,mid=4)
    r.poke(0x2600C+20*5, struct.pack('>5I',0x21C44,*struct.unpack('>4I',r.peek(MODEL+20*4+4,16))))
    P.install(r)
    R=0x4B23C+0x200*5
    r.w8(R+4,b4); r.w8(R+0x62,cls); r.w16(R+0x4E,off4e&0xFFFF); r.w32(R+0x18,ha); r.w32(R+0x10,v10)
    r.w8(0x4A923,band); r.w8(0x4A83D,ext); r.w16(0x4A994,5 if own else 0)
    r.w32(0x4AB1E,-h); r.w32(0x4AB30,-(h>>8))
    r.w16(e,(5<<8)|0x10|lod); r.w16(e+0x10,0)
    if words:
        a=_A2[0]
        for wd in words:
            r.w16(a,wd); a+=6+2*r.u16(a+2)      # distance word, N, flag word, N offsets
    P.clear_log(r); pcs=trace(r,0x1FBCC,0x238C0,0x23B00,a6=0x70000); log=P.read_log(r)
    S=set(pcs)
    ex='closed1' if 0x2394C in S else 'minus1' if 0x238C2 in S else 'open' if 0x23980 in S else None
    ret=[(P.s16(x[2]),(x[3]>>3)&1) for x in log if x[0]==3 and x[1]==4]
    return ex,ret,pcs.count(0x23ADA)
def main(seed=1,n=300):
    if inputs_missing(): print('skipped'); return 0
    rng=random.Random(seed); bad=0; cnt=collections.Counter()
    hs=[0,0x1000,0x1FFF,0x2000,0x2001,0xFFFF,0x10000,0x10001,0x9FFFF,0xA0000,0xA0001,0xFFFFF,0x100000,0x100001,0x180000]
    for _ in range(n):
        b4=rng.choice((0,0x40,0x80,0xC0)); band=rng.randint(0,3); own=rng.random()<.5; cls=rng.choice((0x11,0x30,0x14,0x20))
        h=rng.choice(hs); off=rng.choice((0,0x10,-0x10,0x100,-0x2000)); ext=rng.random()<.2; lod=rng.choice((0,0,1,2,3)); v10=rng.choice((0,0x800,0x801,0xA00,0xA01))
        ha=h+rng.choice((0,0,1,-1,2,-2,0x40,-0x40))
        words=None
        if rng.random()<.4: words=[0xA00]+[rng.choice((0,0x400,0x800,0xA00,0xC00)) for _ in range(3)]
        ex,ret,ncl=run(b4,band,own,cls,h,off,ext,lod,v10,ha,words)
        want=gate_pred(b4,band,own,cls,h,off,ext,lod)
        wret={'closed1':[(1,0)],'minus1':[(-1,1)],'open':[(1,0)]}[want]
        wn=0
        fw=words or FACES
        if want=='open' and (fw[0]<<8)>=ha:
            wn=1
            for dd in fw[1:]:
                if dd<v10: break
                wn+=1
        ok=(ex==want or (want=='minus1' and ex is None)) and ret==wret and ncl==wn
        cnt[(want,ncl)]+=1; cnt['face words not descending' if words and any(a<b for a,b in zip(words,words[1:])) else 'shipped or descending words']+=1; cnt['record altitude differs from the camera height']+=(ha!=h)
        if not ok:
            bad+=1
            if bad<8: print('DIFF',b4,band,own,hex(cls),hex(h),off,ext,lod,hex(v10),'pred',want,wn,'rig',ex,ret,ncl)
    print('op4:',n,'constructed player/other-record entries, gate exits',{k:v for k,v in cnt.items() if isinstance(k,tuple)},{k:v for k,v in cnt.items() if isinstance(k,str)},'differences',bad)
    return bad
if __name__=='__main__':
    import sys
    sys.exit(1 if main(int(sys.argv[1]) if len(sys.argv)>1 else 1,int(sys.argv[2]) if len(sys.argv)>2 else 300) else 0)

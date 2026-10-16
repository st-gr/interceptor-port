"""Entity (record) entries through the real draw path, and the OR rule of the handler returns (04a 8b rule 9, E1-E4).
   python3 ent_check.py [entities|orrule|all] SEED N       skips with exit 0 without the extracted/ inputs
entities: random frames with 0 to 3 further active records in the viewer's cell; run-time descriptors filled the way the game does
  (slot 0: model 3 or 4 with routine 0x21C44, other slots: a model record copied unchanged); only the filler, line and point
  routines are stubbed (primlog).  For every entity entry of queue C the walker's stored +0x14 is compared with the rule: positive
  iff an early exit returned 1 or the OR of the handler returns that did not abandon is positive.
orrule: constructed terrain entries (plain and type B drawers) against the same rule."""
from ent_common import *
AIRCRAFT=(2,3,4,5,6,7,10,11,13,15)
def lt(sr): return ((sr>>3)&1)^((sr>>1)&1)
def predict(calls, typeb):
    """result sign from the handler calls of one entry: (op, d0, sr) in order; None when no handler ran"""
    if not calls: return None
    acc=0
    for op,d0,sr in calls:
        if typeb:
            if d0<0: return -1                      # a negative word aborts the whole type B drawer with 0 (stored as -1)
            acc|=d0&0xFFFF
        else:
            if lt(sr): continue                     # abandoned program: nothing is added (the tree walk goes on)
            acc|=d0&0xFFFF
    return 1 if (acc&0xFFFF) and not (acc&0x8000) else -1
def exits(pcs):
    S=set(pcs)
    for name,a in (('E1 secondary (21C4C)',0x21C4C),):
        pass
    if 0x21C30 in S and 0x21C4C in S and 0x21C4E not in S: return 'E1'
    if 0x21CDC in S: return 'E2'
    if 0x21D98 in S and 0x21F0C in S and 0x21F10 not in S: return 'E4'
    return None
def check_entities(r,seed,n):
    rng=random.Random(seed); bad=0; tot=collections.Counter(); show=0
    for k in range(n):
        tag=('deck','air')[k&1]
        W.random_frame(r,rng,tag=tag,active=True,extra=rng.randint(0,3),camr=rng.choice((200,3000)))
        # descriptors the way the game fills them
        player_desc(r,rng.choice((0,1,3,4)))
        for i in range(1,14):
            R=0x4B23C+0x200*i
            if r.u8(R+1)&0x40: slot_desc(r,i,rng.choice(AIRCRAFT))
        # randomised view state
        r.w8(0x4A923,rng.randint(0,3)); r.w8(0x4A83D,rng.random()<.25); r.w8(0x4A8ED,rng.random()<.25)
        r.w8(0x4A85F,rng.choice((0,0,3,5,9,10)))
        for i in range(16):
            R=0x4B23C+0x200*i
            if r.u8(R+1)&0x40:
                r.w32(R+0x14,(r.u32(0x4AB1A)&0xFFFFF)+rng.randint(-60000,60000)&0xFFFFF); r.w32(R+0x1C,(r.u32(0x4AB22)&0xFFFFF)+rng.randint(-60000,60000)&0xFFFFF)
                r.w32(R+0x18,-r.s32(0x4AB1E)+rng.randint(-30000,30000)); r.w8(R+4,rng.choice((0,0,0xC0,0x40)))
        P.install(r)
        ents=[e for e in W.window(r,'C') if r.u8(e+1)&0x10 and not r.u8(e+1)&0x40]
        P.clear_log(r); pcs=trace(r,W.WALKER['C'],0x21C30,0x22A00,a6=0x70000); log=P.read_log(r)
        for i,pc in enumerate(pcs):
            if pc==0x21CDC: tot['exit E2 (alt list 0xFFFF)']+=1
            elif pc==0x21C30 and pcs[i-1]==0x21C4C: tot['exit E1 (secondary)']+=1
            elif pc==0x21D98: tot['return 0 at 21D98 from 0x%X'%pcs[i-1]]+=1
            elif pc==0x21D92: tot['return 1 tier substitution']+=1
        for e in ents:
            idn=r.u8(e)
            calls=[(x[1],P.s16(x[2]),x[3]) for x in log if x[0]==3 and x[4]==idn]
            prims=sum(1 for x in log if x[0]==4 and x[4]==idn)
            res=r.s16(e+0x14)
            want=predict(calls,False)
            kind='calls' if calls else 'early'
            tot[(kind,res)]+=1
            if want is not None:
                tot['rule-checked']+=1
                if want!=res:
                    bad+=1
                    if show<5: show+=1; print('DIFF entity',idn,'calls',calls,'stored',res,'rule',want)
            elif res>0: tot['early exit positive']+=1
    print('entities: %d frames, %d entity entries; entries with handler calls %d (all follow the OR rule: %d differences); early exits: stored -1 %d, stored +1 %d'
          %(n,sum(v for kk,v in tot.items() if isinstance(kk,tuple)),tot['rule-checked'],bad,tot[('early',-1)],tot[('early',1)]))
    print('   ',dict((str(a),b) for a,b in tot.items()))
    return bad
def check_orrule(r,seed,n):
    rng=random.Random(seed); bad=0; tot=collections.Counter(); show=0
    r.reset()
    items=[i for i in range(17,128) if r.u32(0x2600C+20*i) in (0x21C40,0x21C34,0x21D0C,0x22CFC,0x22D0A) and r.u32(0x2600C+20*i+4)>0x10000]
    for k in range(n):
        W.random_frame(r,rng,camr=300)
        h=rng.randint(0x400,0x14000) if rng.random()<.7 else rng.randint(0x14000,0x300000)
        r.w32(0x4AB1E,-h); r.w32(0x4AB30,(-h)>>8)
        tier=rng.choice('ABC'); ents=[e for e in W.window(r,tier) if not r.u8(e+1)&0x50]
        if not ents: continue
        e=ents[0]; it=rng.choice(items); lod=rng.randint(0,6); spread=rng.choice((100,400,1500))
        r.w16(e,(it<<8)|(r.u8(e+1)&0xF0)|lod); r.w32(e+2,0x2600C+20*it)
        cx=struct.unpack('>h',r.peek(0x4AB2A,2))[0]; cz=struct.unpack('>h',r.peek(0x4AB2E,2))[0]
        r.w16(e+6,((-cx)>>lod)+rng.randint(-spread,spread)); r.w16(e+8,0); r.w16(e+10,((-cz)>>lod)+rng.randint(-spread,spread))
        r.w16(e+0x10,0); r.w16(e+0x14,0); r.w8(e+0x12,0); r.w16(e+24,0xFFFF)
        typeb=r.u32(0x2600C+20*it) in (0x22CFC,0x22D0A)
        P.install(r); r.w8(0x4A974,3); r.w16(0x4A992,rng.randint(0,255))
        P.clear_log(r); W.walk(r,tier); log=P.read_log(r)
        calls=[(x[1],P.s16(x[2]),x[3]) for x in log if x[0]==3]
        res=r.s16(e+0x14); want=predict(calls,typeb)
        if want is None: tot['no handler ran']+=1; continue
        tot['typeB' if typeb else 'plain']+=1
        if want!=res:
            # a stream that ends with 0xFFFF returns 0 in the plain/common drawer (04a 8b rule 9)
            bad+=1; 
            if show<6: show+=1; print('DIFF item',it,'typeB',typeb,'lod',lod,'calls',calls,'stored',res,'rule',want)
    print('orrule: %d constructed entries with handler calls (plain %d, type B %d), %d differences; no handler ran: %d'%(tot['plain']+tot['typeB'],tot['plain'],tot['typeB'],bad,tot['no handler ran']))
    return bad
def main(argv):
    miss=inputs_missing()
    if miss: print('skipped (missing: %s)'%', '.join(miss)); return 0
    mode=argv[1] if len(argv)>1 else 'all'; seed=int(argv[2]) if len(argv)>2 else 1; n=int(argv[3]) if len(argv)>3 else 200
    r=Rig(); bad=0
    if mode in ('entities','all'): bad+=check_entities(r,seed,n)
    if mode in ('orrule','all'): bad+=check_orrule(r,seed,n*3)
    print('ent_check:','FAILED %d'%bad if bad else 'ok'); return 1 if bad else 0
if __name__=='__main__':
    import sys; sys.exit(main(sys.argv))

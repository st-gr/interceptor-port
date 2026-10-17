"""P1 zoom sweep and rule 5 below zoom 0x80 (04a 8b): the rig's walkers against the port-free model at the zoom words
0x20, 0x40, 0x80, 0xFF and 0x100 with V:4A895 zero and non-zero; at zoom below 0x80 queue A/B entries whose item id occurs in queue C
are made to occur (the id byte of a terrain entry of queue C is overwritten).   python3 zoom_check.py SEED N"""
from ent_common import *
def check_zoom(r, seed=1, n=60):
    rng=random.Random(seed); bad=0; show=[0]; stats=collections.Counter()
    for k in range(n):
        for zoom in (0x20,0x40,0x80,0xFF,0x100):
            for flag in (0,1):
                W.random_frame(r,rng,camr=rng.choice((300,3000)))
                r.w16(0x4AAFA,zoom); r.w8(0x4A895,flag)
                if zoom<0x80:                       # make the item ids of tier A occur in queue C (rule 5, third case)
                    ea=[e for e in W.window(r,'A') if not r.u8(e+1)&0x50]
                    ec=[e for e in W.window(r,'C') if not r.u8(e+1)&0x50]
                    for a,c in zip(ea[:3],ec[:3]): r.w8(c,r.u8(a))
                r.w8(0x4A974,3); r.w16(0x4A992,rng.randint(0,255))
                for tier in 'ABC':
                    m=W.snap(r); ents=W.window(r,tier)
                    pred=vis.walk_queue(m,tier); W.walk(r,tier)
                    st=collections.Counter()
                    bad+=W.cmp_model(r,tier,m,pred,st,show)
                    stats['entries']+=len(pred); stats['decided']+=st['decided']; stats['rule 4']+=st['rule 4']; stats['rule 5']+=st['rule 5']; stats['rule 8']+=st['rule 8']
                    if zoom<0x80: stats['rule 5 at zoom<0x80']+=st['rule 5']
                    stats['zoom 0x%X flag %d decided'%(zoom,flag)]+=st['decided']
    print('zoom: %d frames x 5 zoom words x V:4A895 0/1: %d walked entries, %d decided by the model (rule 4: %d, rule 5: %d of which at zoom below 0x80: %d, rule 8: %d), %d differences'
          %(n,stats['entries'],stats['decided'],stats['rule 4'],stats['rule 5'],stats['rule 5 at zoom<0x80'],stats['rule 8'],bad))
    for kk in sorted(stats):
        if kk.startswith('zoom'): print('   %-28s %d'%(kk,stats[kk]))
    return bad
if __name__=='__main__':
    import sys
    if inputs_missing(): print('skipped (missing: %s)'%', '.join(inputs_missing())); sys.exit(0)
    r=Rig(); sys.exit(1 if check_zoom(r,int(sys.argv[1]) if len(sys.argv)>1 else 1,int(sys.argv[2]) if len(sys.argv)>2 else 60) else 0)

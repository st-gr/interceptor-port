"""Finite check of the queue capacity: maximum generated entries per tier over every origin cell and pattern (04a, section 3)."""
import numpy as np, itertools, struct, sys
from blob import *
from model import cell_items, TAB
def sbyte(a): return struct.unpack('>b',rb(a,1))[0]
cnt={}
for T in 'ABC':
    c=np.zeros((128,128,16),dtype=np.int32)
    for row in range(128):
        for key in range(128):
            it=cell_items(T,row,key)
            if it:
                for (lay,i,dx,dz) in it: c[row,key,lay]+=1
    cnt[T]=c
    print(T,'cells with items',int((c.sum(2)>0).sum()),'max items in one slot',int(c.max()),'max items per cell',int(c.sum(2).max()))
def wrap(v,lim):
    v=np.where(v<0,lim,v); return np.where(v>lim,0,v)
def lists_for(T):
    # yield (label, tiles[(dx,dz)], subs-lists) for every parameter combo
    out=[]
    for ph in range(16):
        k=ub(0x4625c+ph); j=ub(0x4628c+ph)
        tp=rb(0x462dc+24*k,24); sp=rb(0x4629c+16*k,16)
        def mk(l):
            return [((sbyte(0x20674+2*tp[t]),sbyte(0x20674+2*tp[t]+1)),[sp[s] for s in subs]) for t,subs in l]
        # HI
        if True:
            for oc in range(8):
                idx=8*j+ub(0x4626c+8*k+oc); l=parse(0x4659c,sw(0x4659c+2*idx))[0]
                out.append((('HI',ph,oc),mk(l)))
            idx=ub(0x4628c+j); l=parse(0x4633a,sw(0x4633a+2*idx))[0]
            out.append((('GATE',ph),mk(l)))
        if T=='A':
            for sub in range(16):
                idx=16*j+ub(0x4629c+16*k+sub); l=parse(0x4717e,sw(0x4717e+2*idx))[0]
                out.append((('LOW',ph,sub),mk(l)))
    return out
res={}
for T in 'ABC':
    n=128 if T=='C' else 32; lim=127 if T=='C' else 30
    o=np.arange(n); c=cnt[T]
    best={}
    for lab,tiles in lists_for(T):
        tot=np.zeros((n,n),dtype=np.int32)  # [oz,ox]
        for (dx,dz),subs in tiles:
            xs=wrap(o+dx,lim); zs=wrap(o+dz,lim)
            sub=c[:,:,subs].sum(2) if subs else np.zeros((128,128),int)
            tot+=sub[np.ix_(zs,xs)]
        kind=lab[0]
        m=int(tot.max()); oz,ox=np.unravel_index(tot.argmax(),tot.shape)
        if kind not in best or m>best[kind][0]: best[kind]=(m,lab,int(ox),int(oz),int((tot>=70).sum()),int((tot>30).sum()))
    res[T]=best
    print(T,best)
print('--- all states with G>=70')
for T in 'A':
    n=32; lim=30; o=np.arange(n); c=cnt[T]
    for lab,tiles in lists_for(T):
        tot=np.zeros((n,n),dtype=np.int32)
        for (dx,dz),subs in tiles:
            xs=wrap(o+dx,lim); zs=wrap(o+dz,lim)
            sub=c[:,:,subs].sum(2) if subs else np.zeros((128,128),int)
            tot+=sub[np.ix_(zs,xs)]
        for oz,ox in zip(*np.where(tot>=70)): print(T,lab,'origin x',ox,'z',oz,'G',tot[oz,ox])

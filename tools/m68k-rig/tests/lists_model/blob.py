import struct
from common import image
IMG=image()
def rb(a,n): return IMG[a-0x10000:a-0x10000+n]
def ub(a): return IMG[a-0x10000]
def sw(a): return struct.unpack('>h',rb(a,2))[0]
def parse(base,off):
    p=base+off; lst=[]
    while ub(p)<0x80:
        t=ub(p); n=ub(p+1); lst.append((t,tuple(ub(p+2+i) for i in range(n)))); p+=2+n
    return lst,ub(p),p+1-base
def index(blob,mode,ph,sub,octant):
    # mode: 'C','A'(hi),'LOW','GATE'; returns word index
    if mode=='LOW':
        j=ub(0x4628c+ph); k=ub(0x4625c+ph); sl=ub(0x4629c+16*k+sub); return 16*j+sl
    j=ub(0x4628c+ph); k=ub(0x4625c+ph)
    if mode=='GATE': return ub(0x4628c+j)
    return 8*j+ub(0x4626c+8*k+octant)
if __name__=='__main__':
    for base,nw in ((0x4633a,4),(0x4659c,32),(0x4717e,64)):
        offs=[sw(base+2*i) for i in range(nw)]
        print(hex(base),'nwords',nw,'distinct offs',len(set(offs)),'first',hex(offs[0]),'sorted-unique',sorted(set(offs))[:5])
        ends=[]
        for o in sorted(set(offs)):
            l,term,end=parse(base,o); ends.append((o,len(l),sum(len(x[1]) for x in l),hex(term),end))
        print(ends[:4],'...',ends[-1], 'maxtiles',max(e[1] for e in ends),'mintiles',min(e[1] for e in ends))
    # reachability
    import itertools
    for mode,base,nw in (('GATE',0x4633a,4),('HI',0x4659c,32),('LOW',0x4717e,64)):
        s=set()
        for ph in range(16):
            for sub in range(16):
                for oc in range(8):
                    s.add(index(base,mode,ph,sub,oc))
        print(mode,'reachable indices',len(s),sorted(s)[:40])

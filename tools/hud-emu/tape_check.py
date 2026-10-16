import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from util import *
def trunc(a,b): return int(a/b)
def model(t):
    r=t%100
    if r>40: B10=t-r+100; s=r-100
    else: B10=t-r; s=r
    B=(B10//10)%360
    o=-trunc(s,5)
    labs=[(156+o,'%02d'%(B//10))]
    if o<=2: labs.append((156+o+20,'%02d'%(((B+10)%360)//10)))
    if o>=-2: labs.append((156+o-20,'%02d'%(((B-10)%360)//10)))
    ticks=set()
    base=159+o
    for k in range(-12,13):
        x=base+10*k
        if 133<x<185:
            major = (k%2==0)
            ticks.add((x,major))
    return sorted(labs),ticks
e=fresh(); bad=[]
import random
random.seed(1)
samples=list(range(0,3600,11))+[3599,3598,0,39,40,41,45,46,99,100,101,140,141,399,400,401,3590,3559,3560]
for t in samples:
    e.w(0x4b23c+0x68,t*8)
    for p in e.pl[:4]: e.uc.mem_write(p,bytes(8000))
    e.glyphs=set(); e.call(0x37d78); px=pix(e)
    g=sorted((x,c) for (y,x,c) in e.glyphs if y==53)
    labs=sorted((g[i][0],g[i][1]+g[i+1][1]) for i in range(0,len(g),2))
    ml,mt=model(t)
    t61=set(x for (x,y),v in px.items() if y==61 and v==13 and 100<x<230)
    t60=set(x for (x,y),v in px.items() if y==60 and v==13 and 100<x<230)
    sim_t=set((x,x in t60) for x in t61)
    if labs!=sorted(ml) or sim_t!=mt: bad.append((t,labs,sorted(ml),sorted(sim_t),sorted(mt)))
print(len(samples),len(bad))
for b in bad[:5]: print(b)

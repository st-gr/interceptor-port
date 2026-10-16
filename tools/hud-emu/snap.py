import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import struct,sys
CHIP=0x61caee
def load(p): return open(p,'rb').read()
def off(a):
    if a<0x80000: return CHIP+a
    if 0xc00000<=a<0xc80000: return CHIP+0x80000+(a-0xc00000)
    raise ValueError(hex(a))
def hunks(d):
    a=0xc07b28; r=[]
    while True:
        o=off(a); size=struct.unpack('>I',d[o-8:o-4])[0]; nxt=struct.unpack('>I',d[o-4:o])[0]
        r.append((a,size))
        if nxt==0 or len(r)>=185: break
        a=(nxt<<2)+4
    return r
if __name__=='__main__':
    d=load(sys.argv[1]); h=hunks(d); print(len(h)); print([(i,hex(a),hex(s)) for i,(a,s) in enumerate(h) if i in(0,1,2,36,71,72,73)])

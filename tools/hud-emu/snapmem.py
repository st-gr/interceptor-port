import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import sys,struct,json
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import snap
m=json.load(open(ROOT+'/extracted/interceptor.map.json'))['hunks']
class S:
    def __init__(s,p):
        s.d=snap.load(p); s.h=snap.hunks(s.d)
    def real(s,flat):
        for i,hh in enumerate(m):
            if hh['addr']<=flat<hh['addr']+hh['size']:
                return s.h[i][0]+flat-hh['addr']
        raise ValueError(hex(flat))
    def b(s,flat,n=1): o=snap.off(s.real(flat)); return s.d[o:o+n]
    def w(s,flat): return struct.unpack('>h',s.b(flat,2))[0]
    def l(s,flat): return struct.unpack('>i',s.b(flat,4))[0]
    def u8(s,flat): return s.b(flat,1)[0]

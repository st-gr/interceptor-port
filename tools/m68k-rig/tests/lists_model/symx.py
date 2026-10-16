"""Symbolic executor for the straight-line vertex-synthesis fragments (ops 42, 43, 44, 59): word arithmetic only."""
import re, sys, ctypes, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import IMGPATH, LIBPATH
_D = {}
def _load():
    if not _D:
        L = ctypes.CDLL(LIBPATH)
        L.m68k_disassemble_raw.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        _D['L'] = L; _D['d'] = open(IMGPATH, 'rb').read()
    return _D['L'], _D['d']
def lines(a,b):
    L, d = _load()
    pc=a; out=[]
    while pc<b:
        buf=ctypes.create_string_buffer(256); ch=d[pc-0x10000:pc-0x10000+20]
        n=L.m68k_disassemble_raw(buf,pc,ch,ch,1); out.append((pc,buf.value.decode())); pc+=n
    return out
# expression: ('v',name) | ('add',a,b) | ('sub',a,b) | ('neg',a) | ('asr',n,a) | ('c',0)
def V(n): return ('v',n)
def ev(e,env):
    t=e[0]
    def w(x): x&=0xffff; return x-0x10000 if x&0x8000 else x
    if t=='v': return env[e[1]]
    if t=='add': return w(ev(e[1],env)+ev(e[2],env))
    if t=='sub': return w(ev(e[1],env)-ev(e[2],env))
    if t=='neg': return w(-ev(e[1],env))
    if t=='asr': return w(ev(e[2],env)>>e[1])
    if t=='c': return e[1]
def lin(e):
    """flatten to (dict sym->coef, [asr atoms]) printing form"""
    t=e[0]
    if t=='v': return {e[1]:1}
    if t=='c': return {}
    if t=='add' or t=='sub':
        a=lin(e[1]); b=lin(e[2]); s=1 if t=='add' else -1
        r=dict(a)
        for k,v in b.items(): r[k]=r.get(k,0)+s*v
        return {k:v for k,v in r.items() if v}
    if t=='neg': return {k:-v for k,v in lin(e[1]).items()}
    if t=='asr': return {('asr',e[1],pp(e[2])):1}
def pp(e):
    l=lin(e); parts=[]
    for k,v in sorted(l.items(),key=lambda kv:str(kv[0])):
        name=k if isinstance(k,str) else '[%s>>%d]'%(k[2],k[1])
        c='' if abs(v)==1 else str(abs(v))+'*'
        parts.append(('+' if v>0 else '-')+c+name)
    s=''.join(parts) or '0'
    return s[1:] if s[0]=='+' else s
class M:
    def __init__(s):
        s.R={}; s.mem={}; s.reads=set()
    def rd(s,off):
        if off in s.mem: return s.mem[off]
        s.reads.add(off); return V('s%d.%d'%(off//6,(off%6)//2))
    def reg(s,r): return s.R.get(r, V('?'+r))
def regs(lst):
    out=[]
    for part in lst.split('/'):
        if '-' in part:
            a,b=part.split('-'); k=a[0]; out+= [k+str(i) for i in range(int(a[1]),int(b[1])+1)]
        else: out.append(part)
    return out
def run(a,b,m):
    for pc,t in lines(a,b):
        mn,_,ops=t.partition(' '); ops=ops.strip(); mn=mn.strip()
        if mn=='rts': return
        o=[x.strip() for x in re.split(r',\s*(?![^()]*\))',ops)] if ops else []
        def off(x):
            mm=re.match(r'\(\$?(-?[0-9a-f]*),A3\)$',x)
            if mm: return int(mm.group(1) or '0',16)
            if x=='(A3)': return 0
            return None
        if mn=='movem.w':
            if off(o[0]) is not None:
                base=off(o[0])
                for i,r in enumerate(regs(o[1])): m.R[r]=m.rd(base+2*i)
            else:
                base=off(o[1])
                for i,r in enumerate(regs(o[0])): m.mem[base+2*i]=m.reg(r)
        elif mn in('add.w','sub.w'):
            src=o[0]
            if off(src) is not None: sv=m.rd(off(src))
            else: sv=m.reg(src)
            dst=o[1]; m.R[dst]=(mn[:3],m.reg(dst),sv)
        elif mn=='asr.w': m.R[o[1]]=('asr',int(o[0][1:]),m.reg(o[1]))
        elif mn=='neg.w': m.R[o[0]]=('neg',m.reg(o[0]))
        elif mn in('move.w','movea.w'): m.R[o[1]]=m.reg(o[0])
        elif mn=='adda.w': m.R[o[1]]=('add',m.reg(o[1]),m.reg(o[0]))
        elif mn=='suba.w': m.R[o[1]]=('sub',m.reg(o[1]),m.reg(o[0]))
        elif mn=='moveq': pass
        else: raise Exception('unsupported %x %s'%(pc,t))
def stores(m): return m.mem
def report(name,m):
    print(name)
    byslot={}
    for off,e in sorted(m.mem.items()):
        k,ax=off//6,(off%6)//2
        byslot.setdefault(k,{})[ax]=e
    for k,axes in sorted(byslot.items()):
        ex=[pp(axes[a]) if a in axes else '-' for a in (0,1,2)]
        # axis-free: strip axis suffix
        strip=[re.sub(r'\.\d','',x) for x in ex]
        same = strip[0]==strip[1]==strip[2]
        print('  slot b+%d: %s %s'%(k,strip[0], '' if same else 'AXES DIFFER %s'%ex))
if __name__=='__main__':
    m=M(); run(0x253ea,0x2551c,m); report('op42 helper 253ea',m)
    m=M(); run(0x2526a,0x25342,m); report('op43 body 2526a',m)
    m=M(); run(0x25360,0x2539a,m); report('op44 body 25360',m)
    m=M(); run(0x25b3c,0x25cdc,m); report('op59 helper 25b3c',m)

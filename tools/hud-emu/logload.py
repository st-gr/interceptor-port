import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import re,sys
def load_log(path):
    tags={}
    for line in open(path):
        m=re.match(r'^(\S+)\s+F:([0-9A-Fa-f]+)\s+([0-9A-Fa-f ]+)$',line.strip())
        if m:
            tags[m.group(1)]=(int(m.group(2),16),bytes.fromhex(m.group(3).replace(' ','')))
    return tags
if __name__=='__main__':
    t=load_log(ROOT+'/extracted/captures/logs/hud.txt')
    for k,(a,b) in t.items(): print(k,hex(a),len(b))

#!/usr/bin/env python3
"""Note 07 section 5: the training lead (record 4, a real placed class 0x10 aircraft) given the hit state that the gun's third hit leaves
(W0 bit 0x0200, +0x20 bit 1), in the real world update of training part 1.  python3 gen-combat-dying-lead.py [samples] > combat-dying-lead.txt"""
import sys
n = int(sys.argv[1]) if len(sys.argv) > 1 else 160
R = 0x4BA3C
o = ['restore extracted/captures/training-code-3.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json',
     'code @disks/countercodes.txt 194A', 'freeze', 'stepuntil F:4A851 1 gt 0 4000', 'step 1']
def sample(k):
    o.append('peek F:%X 2 s%d_w0' % (R, k)); o.append('peek F:%X 2 s%d_w2' % (R + 2, k)); o.append('peek F:%X 2 s%d_c' % (R + 0x4C, k))
    o.append('peek F:%X 1 s%d_st' % (R + 5, k)); o.append('peek F:%X 1 s%d_b20' % (R + 0x20, k))
    o.append('peek F:4A992 2 s%d_n' % k); o.append('peek F:4A963 1 s%d_k' % k); o.append('peek F:%X 4 s%d_y' % (R + 0x18, k))
for k in range(n):
    sample(k)
    if k == 20:
        o.append('poke F:%X 2 0x%04X' % (R, 0x11C8 | 0x0200))
        o.append('poke F:%X 1 0x%02X' % (R + 0x20, 0x02))
    o.append('step 1')
print('\n'.join(o))

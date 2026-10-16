#!/usr/bin/env python3
"""Note 07 section 6.5: a real ejection (airborne.vasnap, SHIFT and E) with the seat lowered to 60,000 units after it has spawned, then the seat's
landing and the end of the flight sampled on every video frame.  python3 gen-combat-seat-landing.py > combat-seat-landing.txt"""
S = 0x4B43C; P = 0x4B23C
o = ['restore extracted/captures/airborne.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze',
     'keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT', 'step 120',
     'poke F:%X 4 0x%08X' % (S + 0x18, 60000), 'stepuntil F:%X 1 and 0x80 4000' % S]
for k in range(110):
    o.append('step 1')
    o.append('peek F:%X 2 s%d_w0' % (S, k)); o.append('peek F:%X 2 s%d_c' % (S + 0x4C, k)); o.append('peek F:%X 4 s%d_y' % (S + 0x18, k))
    o.append('peek F:%X 2 s%d_pw0' % (P, k)); o.append('peek F:%X 1 s%d_nib' % (P + 0x7C, k))
    for v, ln in (('4A992', 2), ('4A8E2', 1), ('4A8E4', 1), ('4A850', 1), ('4A87D', 1), ('4A984', 2), ('4A95E', 1), ('4A994', 2)):
        o.append('peek F:%s %d s%d_%s' % (v, ln, k, v))
print('\n'.join(o))

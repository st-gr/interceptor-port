#!/usr/bin/env python3
"""Note 07 sections 3 and 4: the demo's MiG (record 10) on every video frame from pass 284 on, 168 bytes of the record each.  python3 gen-combat-demo-mig.py > combat-demo-mig.txt"""
o = ['restore extracted/captures/demo.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze', 'peek F:4A992 2 start_n', 'stepuntil F:4A992 2 gt 283 9000']
R = 0x4B23C + 0x200 * 10
for k in range(640):
    o += ['step 1', 'peek F:4A992 2 s%d_n' % k, 'peek F:%X 168 s%d_r' % (R, k)]
print('\n'.join(o))

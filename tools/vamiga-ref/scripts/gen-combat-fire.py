#!/usr/bin/env python3
"""Generates the fire capture scripts of note 07 sections 7 and 8 (combat-fire-*.txt).

    python3 gen-combat-fire.py demo [samples] [step_frames]   > combat-fire-demo.txt
    python3 gen-combat-fire.py gun  [samples] [step_frames]   > combat-fire-gun.txt

demo: from extracted/captures/demo.vasnap, runs to the demo's fire event (stream frames 599 to 602, the AIM-120) and logs, per sample, the
header of the 16 entity records, the three weapon records in full, the projectile pool and the variables the launch and the guidance touch.
gun: from extracted/captures/airborne.vasnap, selects the gun (RETURN cycle) and holds SPACE.
"""
import sys
kind = sys.argv[1] if len(sys.argv) > 1 else 'demo'
n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
step = int(sys.argv[3]) if len(sys.argv) > 3 else 1
V1 = ['4A872', '4A86F', '4A87D', '4A968', '4A84F', '4A920', '4A943', '4A944', '4A945', '4A8FB', '4A910', '4A96C', '4A95F', '4A971', '4A976']
V2 = ['4A992', '4AA78', '4A97E', '4A9F2', '4A9F4', '4A9F6', '4A9F8', '4AA7A', '4A994', '4A996']
V4 = ['4AC08', '4AC0C']
out = []
if kind == 'demo':
    out += ['restore extracted/captures/demo.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze',
            'stepuntil F:54E5C 4 eq 0xC5473F 9000']
else:
    out += ['restore extracted/captures/airborne.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
def sample(k):
    out.append('peek F:54E5C 4 s%d_str' % k)
    for r in range(16):
        out.append('peek F:%X 128 s%d_r%d' % (0x4B23C + 0x200 * r, k, r))
    out.append('peek F:4AD2A 1280 s%d_pool' % k)
    out.append('peek F:4B29B 6 s%d_wpn' % k)
    for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
    for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
    for v in V4: out.append('peek F:%s 4 s%d_%s' % (v, k, v))
if kind == 'gun':
    out += ['key RETURN 0.12', 'step 3', 'keydown SPACE']
for k in range(n):
    sample(k)
    out.append('step %d' % step)
if kind == 'gun':
    out.append('keyup SPACE')
print('\n'.join(out))

#!/usr/bin/env python3
"""Generates the ejection capture scripts of note 07 section 6 (combat-seat-*.txt).

    python3 gen-combat-seat.py eject-cockpit  [step_frames] [samples]  > combat-seat-eject-cockpit.txt
    python3 gen-combat-seat.py eject-external [step_frames] [samples]  > combat-seat-eject-external.txt

Starts from extracted/captures/airborne.vasnap, presses SHIFT and E (the ejection key) and logs the records 0 to 3 and the
variables the ejection touches once per sample (step_frames video frames apart).  The external variant switches to the outside
view first (KP-ENTER).
"""
import sys
kind = sys.argv[1] if len(sys.argv) > 1 else 'eject-cockpit'
step = int(sys.argv[2]) if len(sys.argv) > 2 else 4
n = int(sys.argv[3]) if len(sys.argv) > 3 else 250
V1 = ['4A83D', '4A86D', '4A968', '4A951', '4A872', '4A87D', '4A850', '4A96A', '4A8F6', '4A8FD', '4A895', '4A8E4', '4A863', '4A866', '4A910']
V2 = ['4A82C', '4A992', '4AA7A', '4A97A', '4A8E2', '4A9F2', '4A994', '4A996', '4A984']
out = ['restore extracted/captures/airborne.vasnap',
       'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
if kind == 'eject-external':
    out += ['key KPENTER 0.12', 'step 6']
out += ['keydown LSHIFT', 'key E 0.12', 'keyup LSHIFT']
for k in range(n):
    out.append('step %d' % step)
    for r in range(4):
        out.append('peek F:%X 128 s%d_r%d' % (0x4B23C + 0x200 * r, k, r))
    for v in V2:
        out.append('peek F:%s 2 s%d_%s' % (v, k, v))
    for v in V1:
        out.append('peek F:%s 1 s%d_%s' % (v, k, v))
print('\n'.join(out))

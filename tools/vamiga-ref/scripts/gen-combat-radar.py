#!/usr/bin/env python3
"""Generates the radar/designation capture of note 07 section 12 (combat-radar-demo.txt).

    python3 gen-combat-radar.py > combat-radar-demo.txt      (or: gen-combat-radar.py plain, a fixed step from the snapshot)

From extracted/captures/demo.vasnap (the demonstration flight, mode 3, stream step 47; the game runs about 12 video frames per pass) the windows of WINDOWS are sampled; each sample logs the
designation and threat variables, the radar list (10 entries) and the two erase lists, the message-line buffer and the records 0 to 5 and 8 to 13 (128 bytes each).
The demo's event stream presses T at its step 263, R at 329, RETURN at 387 and fires at 599 (note 08 7.3): the log shows the contact, the designation, the range step and the fire.
"""
import sys
BUF = 0xC544E8           # V:54E44 in demo.vasnap: the stream frame i is at BUF + i
# (first stream frame, samples, video frames between samples; the game runs about 12 video frames per pass): around T (263), the fire (599 to 602) and the hit
WINDOWS = [(118, 2, 62), (260, 12, 12), (326, 3, 30), (590, 40, 12), (632, 6, 25)]
if len(sys.argv) > 1 and sys.argv[1] == 'plain':
    WINDOWS = [(47, 130, 5)]
V1 = ['4A925', '4A926', '4A927', '4A93B', '4A93E', '4A93F', '4A920', '4A96C', '4A8F4', '4A8F0', '4A8EF', '4A91A', '4A871', '4A95E']
V2 = ['4A992', '4AA78', '4AA7C', '4A9F2', '4A9F4', '4A9F6', '4A9F8', '4ABFC', '4ABFE', '4A984', '4AB96', '4AB98', '4A994', '4A996']
out = ['restore extracted/captures/demo.vasnap', 'seglist extracted/interceptor.bin extracted/interceptor.map.json', 'freeze']
k = 0
for (f0, cnt, st) in WINDOWS:
    if f0 > 47:
        out.append('stepuntil F:54E5C 4 gt 0x%X 4000' % (BUF + f0 - 1))
    for _ in range(cnt):
        out.append('peek F:54E5C 4 s%d_str' % k)
        for v in V2: out.append('peek F:%s 2 s%d_%s' % (v, k, v))
        for v in V1: out.append('peek F:%s 1 s%d_%s' % (v, k, v))
        out.append('peek F:4AC0C 4 s%d_4AC0C' % k)
        out.append('peek F:4AB30 4 s%d_4AB30' % k)
        out.append('peek F:53368 160 s%d_list' % k)
        out.append('peek F:537C8 80 s%d_erase' % k)
        out.append('peek F:4A8C2 27 s%d_line' % k)
        for r in (0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13):
            out.append('peek F:%X 128 s%d_r%d' % (0x4B23C + 0x200 * r, k, r))
        out.append('step %d' % st)
        k += 1
print('\n'.join(out))

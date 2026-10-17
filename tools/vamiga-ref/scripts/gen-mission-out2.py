#!/usr/bin/env python3
"""Generates the outcome capture scripts of missions F4 to F6 (modes 6 to 8), note 08 sections 12 and 13.

    python3 gen-mission-out2.py prompts                       > mission-prompts.txt      (stage 1: menu F-key to the countercode prompt, one snapshot per mission)
    python3 gen-mission-out2.py out NAME CHALLENGE            > mission-NAME.txt         (stage 2: type the code, run to the flight state, stage and log the outcome)

Stage 1 pokes the log record (pointer V:1D024, record at R:C54498 in credits-rw.vasnap) so that the mission is selectable
(the byte of the previous mission's completion count) and keeps the mission's own count 0 (first-time sub-list, briefing A).
"""
import sys
REC = 0xC54498
UNLOCK = {6: 0x17, 7: 0x18, 8: 0x19}          # record + 0x11 + M
FK = {6: 'F4', 7: 'F5', 8: 'F6'}
SNAP = 'extracted/captures/mission-m%d-prompt.vasnap'
def rt(a): return '0x%X' % (a + 0xBF7B28)      # the state pointer holds run-time addresses (hunk 0 at R:C07B28 in these snapshots)
IMG = ['seglist extracted/interceptor.bin extracted/interceptor.map.json']
def prompts():
    o = []
    for m in (6, 7, 8):
        o += ['restore extracted/captures/credits-rw.vasnap'] + IMG + ['wait 1', 'key SPACE', 'wait 14', 'freeze',
              'poke R:%X 1 1' % (REC + UNLOCK[m]), 'peek R:%X 78 m%d_rec' % (REC, m), 'thaw',
              'key %s 0.3' % FK[m], 'wait 1', 'freeze', 'stepuntil F:1A6BC 4 eq %s 3000' % rt(0x125A8), 'key SPACE 0.12', 'step 2',
              'stepuntil F:1A6BC 4 eq %s 9000' % rt(0x128AA), 'peek F:4A87F 4 m%d_chal' % m, 'peek F:4A95E 1 m%d_mode' % m,
              'peek F:4A7F2 4 m%d_sub' % m, 'peek F:4A962 1 m%d_cap' % m, 'peek F:4ABD4 2 m%d_variant' % m,
              'snapshot ' + SNAP % m, 'frame extracted/captures/logs/mission-m%d-prompt.ppm' % m, 'thaw']
    return o

RECS = {6: (4, 8, 10, 11), 7: (4, 8, 10), 8: (4, 8, 10, 12, 15)}
LOGREC = 0xC54498
def sample(tag, m, recs=(), extra_rec=None, logrec=False, rec0=False):
    """One log group: state, text list, the flag block V:4A850..4A86F (code 4A850, inside bits 4A855, camera bytes 4A865/6C/6D, pause 4A866),
    V:4A8E2..4 (request, the first-start flag, the countdown), V:4A95E..63 (mode, progress, kills 4A963 and the cap 4A962 / count 4A961),
    the script words, the records."""
    o = ['peek F:1A6BC 4 %s_st' % tag, 'peek F:4A802 8 %s_txt' % tag, 'peek F:4A850 32 %s_a850' % tag, 'peek F:4A8E2 4 %s_e2' % tag,
         'peek F:4A95E 6 %s_a95e' % tag, 'peek F:4A83A 4 %s_a83a' % tag, 'peek F:4A876 1 %s_a876' % tag, 'peek F:4A8EB 1 %s_a8eb' % tag,
         'peek F:4A898 1 %s_a898' % tag, 'peek F:4A96A 1 %s_a96a' % tag]
    o += ['peek F:%s 2 %s_w%s' % (v, tag, v) for v in ('4A82C', '4A978', '4AB8E', '4A992', '4A97A', '4A994', '4AA78', '4AA6C', '4A927')]
    if rec0: o.append('peek F:4B23C 128 %s_r0' % tag)
    for r in recs: o.append('peek F:%X 128 %s_r%d' % (0x4B23C + 0x200 * r, tag, r))
    if extra_rec: o.append('peek F:%X 128 %s_rx' % (0x4B23C + 0x200 * extra_rec, tag))
    if logrec: o.append('peek R:%X 78 %s_log' % (LOGREC, tag))
    return o
def start(m, chal):
    # stage 2: from the prompt snapshot to the first flight pass
    return ['restore ' + SNAP % m] + IMG + ['code @disks/countercodes.txt %s' % chal, 'freeze',
            'stepuntil F:1A6BC 4 eq %s 12000' % rt(0x12EFE), 'step 20']
def flight(m, chal):
    o = start(m, chal) + sample('f0', m, RECS[m], logrec=True, rec0=True)
    o += ['snapshot extracted/captures/mission-m%d-flight.vasnap' % m, 'frame extracted/captures/logs/mission-m%d-flight.ppm' % m]
    # settle: 40 samples every 8 frames, positions of the records
    for k in range(1, 31):
        o += ['step 8'] + sample('f%d' % k, m, RECS[m])
    return o

def run_samples(tag, m, n, every, recs=None, logrec=False, extra_rec=None, rec0=False):
    o = []
    for k in range(n):
        o += ['step %d' % every] + sample('%s%d' % (tag, k), m, recs if recs is not None else RECS[m], extra_rec=extra_rec, logrec=logrec and k % 10 == 0, rec0=rec0)
    return o
def poke_pos(x, z, y=None):
    o = ['poke F:4B250 4 0x%X' % x, 'poke F:4B258 4 0x%X' % z]
    if y is not None: o.append('poke F:4B254 4 0x%X' % y)
    return o
def cell(c, half=True): return (c << 22) + (0x200000 if half else 0)
def restore_flight(m): return ['restore extracted/captures/mission-m%d-flight.vasnap' % m] + IMG + ['freeze']
def full(m, chal, part='a'):
    o = flight(m, chal) if part == 'a' else []
    if m == 8 and part == 'a':
        # region triggers: box 3 (cells 75..77 x 75..77) and box 2 (50..76 x 67..76), then back to the carrier
        o += ['# --- branch R: region triggers (staged: the player position is poked into the boxes)'] + restore_flight(8)
        o += poke_pos(cell(76), cell(76), 0x300000) + run_samples('r1_', 8, 40, 8, recs=(4, 12))
        o += poke_pos(cell(70), cell(65), 0x300000) + run_samples('r2_', 8, 40, 8, recs=(4, 12))
        o += poke_pos(cell(60), cell(70), 0x300000) + run_samples('r3_', 8, 40, 8, recs=(4, 12))
    if m == 8 and part == 'b':
        o += ['# --- branch S0: success, no lock held (staged: V:4A963 poked above the cap)'] + restore_flight(8)
        o += ['poke F:4A963 1 8'] + run_samples('s0_', 8, 60, 6, recs=(8,), logrec=True)
        o += ['frame extracted/captures/logs/mission-m8-s0-text.ppm']
        o += ['# --- the landing: the lift-off bit poked, the player sits on the deck', 'poke F:4B25D 1 1'] + run_samples('l0_', 8, 60, 6, recs=(), logrec=True, rec0=True)
        o += ['frame extracted/captures/logs/mission-m8-fc-text.ppm'] + run_samples('l1_', 8, 40, 8, recs=(), logrec=True, rec0=True)
        o += ['# --- branch S1: success with a lock held on record 8 (offset 0x1000)'] + restore_flight(8)
        o += ['poke F:4AA78 2 0x1000', 'poke F:4A963 1 8'] + run_samples('s1_', 8, 24, 6, recs=(8,))
    if m == 7 and part == 'a':
        o += ['# --- branch F: the real failure (the player does nothing), natural parity'] + restore_flight(7)
        o += ['stepuntil F:4A850 1 ne 0 30000', 'snapshot extracted/captures/mission-m7-fail.vasnap'] + run_samples('n0_', 7, 12, 4, recs=(4,))
        o += ['stepuntil F:1A6BC 4 eq %s 3000' % rt(0x131C8)] + run_samples('n1_', 7, 90, 4, recs=(4,), logrec=True)
        o += ['frame extracted/captures/logs/mission-m7-fail-natural.ppm']
    if m == 7 and part == 'b':
        for par, name in ((1, 'odd'), (0, 'even')):
            o += ['# --- branch %s: the same event, V:4A992 poked to %s once the freeze holds it' % (name, name)]
            o += ['restore extracted/captures/mission-m7-fail.vasnap'] + IMG + ['freeze', 'stepuntil F:1A6BC 4 eq %s 3000' % rt(0x131C8), 'step 1', 'poke F:4A992 2 %d' % (0x1001 if par else 0x1000)]
            o += run_samples('%s_' % name, 7, 90, 4, recs=(4,), logrec=True)
            o += ['frame extracted/captures/logs/mission-m7-fail-%s.ppm' % name]
        o += ['# --- branch S: success (staged: the missile record made invalid and wrecked)'] + restore_flight(7)
        o += ['poke F:4BA3D 1 0', 'poke F:4BA5C 1 2'] + run_samples('s_', 7, 60, 6, recs=(4,), logrec=True)
        o += ['frame extracted/captures/logs/mission-m7-s-text.ppm', 'poke F:4B25D 1 1'] + run_samples('l_', 7, 50, 6, recs=(), logrec=True, rec0=True)
    if m == 6 and part == 'a':
        o += ['# --- branch N: the real ejection (SHIFT and F: the parachute) at the pod: player position poked to the pod, 0x100000 higher'] + restore_flight(6)
        o += poke_pos(0x12700000, 0x11100000, 0x400000) + ['keydown LSHIFT', 'key F 0.12', 'keyup LSHIFT'] + run_samples('n_', 6, 120, 6, recs=(11,), logrec=True)
        o += ['frame extracted/captures/logs/mission-m6-near.ppm']
        o += ['# --- branch X: the ejection at the start position (far from the pod)'] + restore_flight(6)
        o += ['keydown LSHIFT', 'key F 0.12', 'keyup LSHIFT'] + run_samples('x_', 6, 120, 6, recs=(11,), logrec=True)
        o += ['frame extracted/captures/logs/mission-m6-far.ppm']
    return o
if __name__ == '__main__':
    if sys.argv[1] == 'prompts':
        print('\n'.join(prompts()))
    elif sys.argv[1] == 'full':
        print('\n'.join(full(int(sys.argv[2]), sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else 'a')))
    elif sys.argv[1] == 'flight':
        print('\n'.join(flight(int(sys.argv[2]), sys.argv[3])))

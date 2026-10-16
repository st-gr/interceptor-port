# 09. DefenseNet security check (manual lookup)

Scope: the security-check prompt (its title line), the copy-protection step in which the game shows a four-character challenge and expects a 3 to 6 digit octal countercode looked up in the manual's code wheel. The port does not reproduce the check; this note documents the original's behaviour and records how the original's expected answers compare with an independent decoder of the code wheel. No challenge or answer values are listed here.

Claims are marked **(verified)** when a Musashi rig run of the original code confirmed them, and **(read)** when they come from reading the disassembly only.

## Location

| item | hunk | address | notes |
| --- | --- | --- | --- |
| prompt strings (the title line, the countercode request line, the compromised-security message) | 64 | `0x44000`-`0x440D0` (text table) | The strings are embedded in a stream of positioned text records (control bytes, then text), so no code holds a plain pointer to the string start. The prompt record has the four challenge characters patched in at byte 31 of the record that starts at `0x440AA`, i.e. at `0x440C9`-`0x440CC`. |
| prompt entry, called once per check | 0 | `0x127C8` (182 bytes) | Runs when the state word at `0x4AB8E` is negative; builds the status line, then calls the challenge builder. Its only branch around the builder is a flag at `0x4A8E3` (read; not investigated). |
| challenge and expected-answer builder | 20 | `0x2963A`-`0x296F9` (192 bytes) | Only caller is the prompt entry above. |
| hex or octal digit to character helper | 20 | `0x296FA`-`0x29709` | Adds `0x30`, plus 7 above 9. |
| answer comparison | 20 | `0x2970A`-`0x2973D` | Compares typed text to the expected string; writes a result byte to `0x4A964` (+1 accept, -1 reject). |
| answer function (the pure computation) | 0 | `0x188E0`-`0x18A2D` and helper `0x18854`-`0x188DF` | Only these two routines and three tables are needed to get the answer from the challenge. |
| tables read by the answer function | 1 (DATA) | `0x1A4D0` (16 words), `0x1A4F0` (16 words), `0x1A510` (4 words) | Initialised data. Contents not reproduced here. |
| time source | 0 and 172 | `0x18E54`; the DoIO stub at `0x58CA8` | Issues a timer-device "get system time" request. |
| working storage | 71 (zero-filled) | seed `0x4ABAE` (microseconds of the last time read); expected string `0x4A883`; typed text `0x4A8A3`; challenge nibble buffer `0x4A87F` | |
| working storage | 2 (BSS) | the four challenge nibble words at `0x1D02A`, `0x1D02C`, `0x1D02E`, `0x1D030` and the answer length code word at `0x1D032` | |

## Behaviour

### Challenge generation

1. The builder first reads the system time through the timer device and takes the microsecond field (a value from 0 to 999999) as the seed. **(read; the time-read request and its field offsets are read, and identified by the DoIO stub and the command code for get-system-time.)**
2. Four nibbles are cut from the seed. Each successive nibble is taken after shifting the seed right by **one** bit, not four: nibble k is bits k to k+3 of the seed. The challenge therefore depends only on the low 7 bits of the seed, and the nibbles overlap. **(verified: rig runs with 1024 seeds whose upper bits were random gave identical results to the 128 seeds with only the low 7 bits set.)**
3. The answer function (below) is then run. Besides the answer it may rewrite the second nibble (when a parity condition on the first nibble and the selector index holds); the rewritten value is what is displayed. The displayed challenge is nibble 1, rewritten nibble 2, nibble 3, nibble 4, each printed as an upper-case hexadecimal digit. **(verified)**
4. Result: only **128** challenges can ever be shown, one per value of the low 7 bits of the microsecond clock. All sixteen characters 0 to F can occur in every position in principle, but the set is a 128-element subset of the 65536 four-hex-digit combinations (0.20 percent). **(verified, 128 seeds give 128 distinct challenges.)** The displayed characters are numbered 1 to 4 from left to right, position 1 being the most significant hex digit. Positions 1, 3 and 4 are the seed's bit windows starting at bit offsets 0, 2 and 3 of the 7-bit seed (position 1 = bits 0 to 3, position 3 = bits 2 to 5, position 4 = bits 3 to 6); position 2 would be the window at bit offset 1 but is rewritten by the game, so it is derived and must not be used for filtering. The overlap gives two constraints: the low 2 bits of position 3 equal position 1 shifted right by 2 (position 3 AND 3 equals position 1 >> 2), and the low 3 bits of position 4 equal position 3 shifted right by 1 (position 4 AND 7 equals position 3 >> 1). Exactly 128 triples of positions 1, 3, 4 satisfy both, and they are the reachable ones (verified against the rig output); the wheel's other 65408 rows are never asked by the game.
5. Distribution: the seed is a time reading, not a pseudo-random generator. The low 7 bits of the microsecond field are close to uniform (1,000,000 / 128 = 7812.5 occurrences per value, so the bias is negligible). The distribution is therefore uniform over the 128 reachable challenges up to how often the prompt is reached at predictable moments; the original has no per-session sequence, so a fresh run can repeat challenges. **(read for the time-based claim; uniformity is arithmetic.)**

### Expected answer

1. The answer function takes the four nibble words. Its helper builds an 8-bit value from the first and last nibbles, one table entry indexed by the fourth nibble and a second table, then runs a fixed 15-step linear feedback sequence on it. The helper also derives a 2-bit length code from the 8-bit value and stores it in the word at `0x1D032`. **(read)**
2. The function returns the 16-bit result masked by one of four masks, selected by the length code. **(read.)** The 15-step structure and the masking are read from the code; only the resulting outputs (displayed challenge, expected string, length) are verified, by rig runs.
3. The builder prints the masked result as an octal number of exactly (length code + 3) digits, leading zeros kept, so answers are 3, 4, 5 or 6 digits long; each length occurs for 32 of the 128 challenges. **(verified)**

So the expected answer is computed from the challenge by an algorithm over small tables; it is not a stored list of 65536 answers, and nothing in the game depends on a codewheel table.

### Validation of the typed answer

The comparison walks the expected string and the typed text in step: it accepts if every one of the (length code + 3) expected characters equals the typed character in the same position, and rejects at the first difference. Characters of the typed text beyond the expected length are not examined by this routine. **(read)** How the typed text is collected (the input loop, the six blank positions after the prompt, handling of too short input, number of retries and the consequence of failure, i.e. the "security has been compromised" screen) was not traced; see Open questions.

## Comparison with the code wheel decoder

The decoder under comparison is the user's `tools/codewheel.py`, together with its generated 65536-row lookup. In the Musashi rig the original's builder was entered at `0x29640`, after the time read, with the seed word at `0x4ABAE` set directly, for every one of the 128 seeds; the displayed challenge and expected answer were compared to the lookup `disks/countercodes.txt` (generated from `tools/codewheel.py`).

| quantity | count |
| --- | ---: |
| challenges the game can show | 128 |
| of which the decoder gives the same answer as the game (digits and length) | 128 |
| disagreements | 0 |
| decoder rows never asked by the game | 65408 |

Result: the game's own answers **agree on 128 of 128 reachable challenges (verified)**. The decoder's remaining 65408 rows cannot be checked against the game because the game never shows those challenges. They can only be said to be consistent with the same wheel construction; if a second reference were wanted, the answer function could also be fed arbitrary nibbles (it accepts any four words), but the displayed challenge then differs from the input whenever the second nibble is rewritten, so that is not a check of the printed manual lookup.

## Constants

| name | value | evidence/address |
| --- | --- | --- |
| seed bits that matter | low 7 (of the microsecond field) | read, builder at `0x2963A`; verified |
| bit step between nibbles | 1 | read, builder at `0x2963A`; verified |
| reachable challenges | 128 | verified |
| answer length | 3 to 6 octal digits, 32 challenges of each length | verified |
| steps in the answer helper's feedback sequence | 15 | read, `0x18888` |
| tables | 16 words, 16 words, 4 words | `0x1A4D0`, `0x1A4F0`, `0x1A510` |
| prompt patch position | `0x440C9` (4 bytes) | read, hunk 64 |

## Open questions

- Input handling: where the typed answer is read, how many attempts are allowed, what happens when the input is shorter than the expected answer, and what failure triggers. Evidence: trace readers of `0x4A8A3` and the writers of `0x4A964`; then run the reference emulator at the prompt (needs the Kickstart 1.2 ROM).
- Whether the prompt can be reached at times when the time source is not the timer device (the branch on `0x4A8E3` in the prompt entry) and what fixes the sequence of prompts in a session.
- Why the answer function sometimes rewrites the second nibble (it is part of how the wheel's print was produced; not needed for the port).

## Verified by

| claim | evidence |
| --- | --- |
| 128 distinct challenges; seed bits above bit 6 irrelevant; answer lengths 3, 4, 5, 6 with 32 each; 128 of 128 agreement with the decoder | A throwaway Musashi rig driver (`tools/m68k-rig`, not committed) loaded the relocated image, set the seed word, ran the builder from `0x29640`, and read the displayed characters, the expected string and the length code. Run for seeds 0 to 127, and again for 1024 seeds with random upper bits. |
| time source is the timer device's system time (microseconds) | Read only: the DoIO stub symbol, the command code 10 and the field offsets in `0x18E54`. Not run, because the rig has no operating system. |
| comparison routine behaviour, input handling | Read only. |

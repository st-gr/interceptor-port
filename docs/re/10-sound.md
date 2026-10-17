# 10. Sound: the library, the sound objects, the per-pass routine, the ramp

Scope: everything the game makes audible. The sound library (hunks 75 to 86), its 37 sound objects and their loader, the two synthesised
objects (the tone generator and the noise buffer), the player and its four voices, the interrupt handlers, the per-pass routine `0x14AA0` that makes
the engine, the wind and the afterburner, the master-volume ramp, the start-up music, the key click and the HUD beeps, and every call site of every
wrapper (the event table of section 8). Notes 05, 06, 07 and 08 name the triggers and refer here for the object, the voice, the period and the
volume; this note owns the numbers.

Tags: **[V]** verified by the Musashi rig (the sound library, the wrappers and the per-pass routine run on the real image and the real sample files, the
custom-register write log as the witness) or by a reference-emulator capture (`audtrace` and `wav`, note 01a); **[R]** read from the code only;
**[C]** fitted or chosen (a port convention); a tag with a count says how many cases agreed. Addresses are flat-image addresses (note 00); `V:` marks
a variable. A *blank* is one vertical blank of the game's PAL display (nominally 50 Hz; the game runs 313 lines, 49.92 blanks a second, 20.03 ms, 4.2); a *pass* is one run of the game's frame routine (note 05, section 14: about 8 per
second in flight, about 6 per blank on the text screens, note 08 section 3.8). Words and long words are big-endian; "hi word" is the upper 16 bits of a
16.16 fixed-point long.

## 1. Location, the library and the hardware use

### 1.1 Location

| what | where | notes |
| --- | --- | --- |
| the sound library | hunks 75 to 86, `0x54FC4` to `0x55DF8` | hardware access `0x54FC4`-`0x55155`, the audio interrupt handler `0x55158`, the per-blank server `0x551D8`, the register writer `0x55260`, the script interpreter `0x55292`, start-up `0x552FC`, shut-down `0x553D8`, the file-size routine `0x55480`, the object allocator `0x55504`, the file loader `0x55626`, the clone routine `0x556AC`, the object free routine `0x5586E`, the square-wave builder `0x55B88`, the random source `0x55BE4`, `0x55C32` and `0x55C66`, the scripts `0x55CA0`-`0x55DF8` [R] |
| library data | hunks 78 to 81 | the device-open request words `0x55454`-`0x5547C` (the channel mask, the port and device names), the 37 object pointers `0x5591C`-`0x559AF`, the file mode string `0x559BC` [R] |
| channel state | hunk 74 area, `0x54EC0`-`0x54FBE` | the table of four channel descriptors `0x54EC0`, the voice slots `0x54ED0` (four longs: the object a voice plays), the descriptors `0x54EE0` (four of `0x1C` bytes), the two interrupt nodes, the vertical-blank node `0x54FA8`, the master volume `V:54FBE` [R] |
| the player and the wrappers | hunk 0, `0x19C58`-`0x1A34A` | stop `0x19C58`, start `0x19C7C`, the start-up phrase `0x19CE6`, the mission tone pair `0x19D7A`, the cockpit engine pair `0x19DB2`, the external engine pair `0x19E46`, the pitch and volume glide `0x19EBE`/`0x19EFA`, the wind `0x19F9A`, the tone `0x1A042`, the noise `0x1A0DC`, the ground contact `0x1A18C`, the gear `0x1A1E6`, the gear stop `0x1A24C`, the launch `0x1A258`, the gun `0x1A2F0`; the stop-all `0x115F6` [R] |
| the loaders | hunk 0, `0x19660` (the two start-up objects), `0x196BA` (the music), `0x199CA` (the engine, wind, ground, gear, tone and noise objects) | called from the start-up routine `0x103CE` (the first two) and the main program `0x11664` (the third) [R] |
| the per-pass routine | `0x14AA0`, with `0x152C6`, `0x1530E`, `0x154E6`, `0x15502` | the cue player is its middle part, `0x14B5A` (note 07, section 13.4) [R] |
| the master-volume ramp | `0x294AC`, called by the game's own vertical-blank server (`0x92DE` in hunk 0, note 04) | [R] |
| key click and HUD beeps | `0x37C2E`, `0x37C32`, `0x37C44`, `0x37C4A`, `0x37C52`; callers `0x376F8`, `0x37B18` | [R] |

### 1.2 The hardware use [R: the code and a whole-image scan; V: the register writes of every sound_check part (about 40,000 logged writes) and the captures of 8.2]

The game never goes through the operating system for sound apart from the set-up. The library, started by `0x552FC` at `0x10404`:

1. allocates one `0x44`-byte request in chip memory, sets its priority byte to 127 and its allocation data to a one-byte mask `0x0F` (all four channels), and
   opens `audio.device` unit 0 with it. The allocation is never given back until `0x553D8`; priority 127 keeps any other client from taking a channel.
   (The request needs a reply port, which the library makes with a seven-letter name of no meaning.) Failure codes 1, 2 and 3 (no memory, no port, device
   refused) end the start-up with no sound system: `0x103CE` calls the shut-down `0x553D8` and the enable longs `V:4AC10`-`V:4AC13` stay 0.
2. installs, by SetIntVector, the audio interrupt handler `0x55158` on the four audio interrupts (levels `INTB_AUD0` to `INTB_AUD3`, descriptor numbers 7 to
   10) with the data pointer = the channel descriptor, and adds the per-blank server `0x551D8` to the vertical-blank chain (`INTB_VERTB`, node priority -120,
   so it runs after the other servers). The hardware access routines `0x54FC4`-`0x5503C` enable or disable one channel's DMA and interrupt and raise its
   interrupt request.

After that the library writes the chip registers directly: for a channel `n` the location `AUDnLC` (a long), the length `AUDnLEN` (in words), the period
`AUDnPER`, the volume `AUDnVOL`, and `DMACON` (set or clear one channel bit together with the master bit: the values are `0x8200 + (1 << n)` and `1 << n`),
`INTREQ` (the acknowledge `0x0080 << n`, the software request `0x8000 + (0x0080 << n)`) and `INTENA`. **The game never writes `AUDnDAT`, `ADKCON` or
any CIA register** [R: no reference to `0xDFF09E`, `0xDFF0n6`-`0xDFF0nA` outside the library's channel pointers, and no 32-bit constant in `0xBFD000`-`0xBFEFFF`
anywhere in the image, whole-image scan]: there is no period or volume modulation, no manual (non-DMA) playback, and the low-pass filter (the power-LED
bit of CIAA port A) is left as the system has it. The stereo pairing is the hardware's, not the game's: voices 0 and 3 go to the left output and voices 1
and 2 to the right. The filter state of the reference machine is in section 1.5.

### 1.3 The four voices

| voice | output | what plays on it |
| --- | --- | --- |
| 0 | left | the engine (cockpit pair, object 0; external pair, object 2), the wind (object 8), the music (the even objects `0xD`-`0x21`), the mission tone (object `0x23`) |
| 1 | right | the same, the second member of each pair (objects 1, 3, 9, the odd music objects, `0x24`); always started together with voice 0 |
| 2 | right | the noise (object 6: thuds, bumps, explosions, the crash), the gun (`0xA`), the ground contact (5), the gear (`0xB`), the missile launch (`0xC`) |
| 3 | left | the tone (object 4: cues, HUD beeps, key clicks, the ejection-seat tone) |

So the engine is heard on both sides (a slightly detuned pair, section 5), the effects of voice 2 on the right only and the tones on the left only [R: every
call of the player names its voice, section 8; V: the capture of the menu, `sound-menu`, shows the voices that move]. Two sounds on one voice cannot coexist:
starting a sound stops what the voice was playing (section 4).

### 1.4 The model the port needs (for the mixer) [R for the hardware facts; V: the register values of the 2,000 passes and 312 tone sets; the sample timing by the music schedule of 4.2]

A voice plays signed 8-bit samples. Each sample lasts `period` colour clocks of the PAL machine, so the rate is 3,546,895 / period samples per second
(PAL; the A500's NTSC clock does not apply: the game is PAL). The library writes the period from the hi word of the object's period field and never
below `0x7C` (124: the hardware minimum; the comparison is signed, so a hi word of `0x8000` or more also becomes `0x7C`, which cannot occur: the largest period is 17,178); the volume is the hi word of the volume field masked with `0x3F` and then limited to the master level's hi word
(section 6), so 0 to 63 [V: 2,000 passes and 312 tone sets, every register write]. The output of a voice is the sample times volume / 64; the four voices
are summed into the left (0 and 3) and right (1 and 2) outputs; there is no interpolation and no envelope other than what the scripts do per blank
(section 3). A voice whose DMA is switched off outputs nothing (the library also writes volume 0 and period `0x7C` then). The sample data of an object is a
byte range of a file (section 2); the block the hardware plays is the whole range, started over at its beginning when it ends (the repeat rule of section 4).

### 1.5 The filter, the CIA state and the machine [V: `sound-cia`, `sound-click`, `sound-crash`; `sound_capture_check.py stereo`]

The game does not touch the filter bit (1.2). On the reference machine (vAmiga, A500, Kickstart 1.2, the credits snapshot) CIAA port A reads `0xFC` with its direction register `0x03`: bit 0 (the overlay) and bit 1 (the power LED, which is
also the audio low-pass filter's switch: **low = LED on = filter on**) are outputs and **both are 0, so the power LED is on and the A500's LED filter is enabled while the game runs** (CIAB port A `0xFF`, direction `0xC0`). The game inherits this
state from the operating system and never changes it. **The replica runs with both filters on.** The A500 has a fixed first-order RC low-pass that is always present (R = 360 ohm, C = 100 nF: corner 4,421 Hz) and the LED filter that the LED bit switches in (a second-order low-pass from two 10 kohm resistors and 6.8 nF and 3.9 nF: corner 3,090.5 Hz, Q about 0.66); the A500 also has a high-pass of about 5.13 Hz (1,390 ohm, 22.33 uF). These are the values of vAmiga's filter model (`Components/Paula/Audio/AudioFilter.cpp`, A500 setup), which is the reference machine. The game inherits the LED bit from Kickstart (low = on), so every machine ran the game with the LED filter on: the mixer's default is **both low-passes and the high-pass on**, with the LED filter switchable off as a labelled option [C: the filters' orders and corners are vAmiga's, cited; "on" follows the captured bit]. The plan's wording "only if the game enables it" tested whether the game uses the filter, not the machine state it runs in.
The stereo pairing holds in the recording of the crash made with hard panning and no filter: the crash noise (voice 2) alone is only in the right output (left 0.0,
right 57.5 RMS) and the cue tone of voice 3 (a 342 Hz and a 554 Hz square wave) only in the left (left 17.7 and 19.0, right 0.9 and 1.1 at the tone's frequency); the music on voices 0 and 1 is the same signal on both sides. The display of the game is 313 lines
long (71,051 colour clocks, 49.92 blanks a second) as the block timings of 4.2 show.

## 2. The sound objects

### 2.1 The object record [R; V: `sound_check.py objects`, 157 checks, and the model's use of every field in all parts]

A sound object is a `0x40`-byte record in fast-or-any memory (allocated with the clear flag); the 37 pointers are the table at `0x5591C` (object number `n` at
`0x5591C + 4n`; a zero pointer means the object does not exist). The sample data is a separate chip-memory block. The fields the library uses:

| offset | size | field | meaning |
| --- | --- | --- | --- |
| +0x00 | long | data | address of the first sample byte (chip memory) |
| +0x04 | long | length | the block length in bytes; bit 31 set: the data is shared with another object (not freed twice); the hardware length is length / 2 words |
| +0x08 | long | period | 16.16: the hi word is the period the library writes (a floor of `0x7C`) |
| +0x0C | long | volume | 16.16: the hi word masked with `0x3F`, limited by the master level, is the volume written |
| +0x10 | long | count | how many blocks the voice still plays of this object: -1 = for ever; see 4.2 |
| +0x14 | long | reload | the count restored when the voice moves on to the next object |
| +0x18 | long | period step | added to the period field once per blank (16.16, signed) |
| +0x1C | long | volume step | added to the volume field once per blank |
| +0x20 | long | next | the object the voice moves on to when the count runs out (0: stop) |
| +0x24, +0x28 | long | loop counters 0 and 1 | used by the script commands of 3.1 |
| +0x2C | long | wait | blanks until the script runs again (0: no script running; 1 right after a start) |
| +0x30 | long | script | the address of the object's script (0: none) |
| +0x34 | long | position | the byte offset of the next script command |
| +0x38, +0x3C | long | step timers | blanks until the period step, or the volume step, is set to 0 again (0: no timer) |

A new object (`0x55504`) has period `0x166` (358), volume `0x3F`, count 1, reload 0, next 0 and everything else 0. A *clone* (`0x556AC`) copies data, length
(with bit 31 set), period, volume, count, steps, next, wait and the timers from another object, so a clone is an independent record on the same sample.

**Objects are persistent records.** A start (the player) writes only the volume field and the voice slot; it never resets the count, the period, the steps or the script position. The count is changed only by the interrupt handler (decrement, and the reload when it falls below 0) and by the ground wrapper (1); the period field by the wrappers and scripts, so an object keeps what the last play left in it (the gun's 280, the music's counts). A port must keep one record per object for the whole session, not a copy per start [V: `sound_check.py music`, the restart below].

### 2.2 The 37 objects [V: `sound_check.py objects`, 157 checks, 0 differences: all 37 pointers (object 7 absent), the fields of every record, the file bytes under each data range, the square wave, the noise buffer, the next links, the enable bits]

The loaders build these in this order (files are read whole from `text/` of the game disk, drive DF0; a file that is not found, or memory that is not available, leaves the
object out and its enable bit clear). "Offset" and "length" are the byte range of the file the object plays; the whole range is one block, so there is no
separate repeat range: the block is replayed from its first byte (section 4.2).

The per-object values (file, offset, length, period, count, reload, next, script, voice and the block time at the object's period) are not listed here: they are the 37 records of `tables/sounds.bin` (740 bytes, FNV-1a-32 `793cb568`). The file is **built by the extractor from the loaders' code rules** (extractor 24): the count, reload, next and voice columns (and the kind, volume and script) are immediates of the loader code, rules and not disk data, which the extractor writes into `sounds.bin` for the core; the only disk-derived part is the sample byte ranges (offset and length into the sample files), identified by the hash. The columns are `u8 file`, `u8 kind`, `u8 voice`, `u8 volume`, `u32 offset`, `u32 length`, `u16 period`, `i8 count`, `u8 reload`, `u8 next`, `u8 script`, `u16` reserved (`tools/extract/README.md`). The object classes, their numbers and how the loaders derive each field are:

| objects | class | file, range and length | period, count, script | voice |
| --- | --- | --- | --- | --- |
| 0, 1 | the cockpit engine pair (`textcpt`) | both whole file from offset 0; object 1 plays only a first part of it (a loader constant), object 0 the whole file | default period, count -1, no script | 0 and 1 |
| 2, 3 | the external engine pair (`textegn`) | object 2 the whole file; object 3 starts at a loader constant offset and plays to the end of the file | default period, count -1 | 0 and 1 |
| 4 | the tone | synthesised, a 32-byte buffer, no file | default period, count -1, script `tone` | 3 |
| 5 | the ground contact (`texttre`) | the whole file | default period (the wrapper sets its own), count 1 | 2 |
| 6 | the noise | synthesised, 2,048 bytes, no file | default period, count -1, script `noise` | 2 |
| 7 | not used | | | |
| 8, 9 | the wind pair (`textwnd`) | object 8 a loader constant length from offset 0; object 9 starts at a loader constant offset and its length is derived from object 3's (object 3's length minus object 9's offset, a quirk that keeps the range inside the file) | default period, count -1 | 0 and 1 |
| `0xA` | the gun, a clone of object 6 | the noise buffer | the period of the gun, count -1, script `gun` | 2 |
| `0xB` | the gear (`textger`) | the whole file | default period, count -1, no script (the gear wrapper sets `gear`) | 2 |
| `0xC` | the launch, a clone of object 2 | the whole of `textegn` | default period, count -1, script `launch` | 2 |
| `0xD` to `0x22` | the music, 11 pairs (even number voice 0, odd number voice 1) | `texti0a`, `texti0b` and `texti1` to `texti5`, whole files from offset 0; most of the later pairs are clones of the same sample data (below) | default period; counts and reloads as the loader wants blocks loaded (4.2); `next` links each pair to the following one | 0 and 1 |
| `0x23`, `0x24` | the start-up pair (`text201`) | the whole file, one object per voice | two loader periods that differ by 2, count -1 | 0 and 1 |


Rules read off the table and the loaders [R, V as above]:

* Objects 0 and 1 share `textcpt`; object 1 plays only a first part of it (so the two voices loop at different lengths and drift in phase). Objects 2 and 3 share
  `textegn`; object 3 starts at a loader constant offset into the file and plays to its end. Objects 8 and 9 share `textwnd`; object 9 starts at a loader constant
  offset in and its length is object 3's length minus that offset (the loader derives it from object 3's length: a quirk that keeps the range inside the file; the
  offsets and the part lengths are loader immediates, in `tables/sounds.bin`). Object `0xC` plays the whole of `textegn` like object 2. Object `0xA` is a clone of
  object 6 (the noise buffer) at the period of the gun.
* The music objects `0xD` to `0x22` are pairs: the even number is voice 0 and the odd voice 1; each plays its `next`, so voice 0 runs `0xD`, `0xF`, `0x11`,
  `0x13`, ... `0x21` and then `0x11` again for ever (the intro `0xD`, `0xF` is played once), voice 1 the same with the odd numbers [V: `sound_check.py music`, 120 block
  ends per voice, 0 differences]. The objects `0x15`-`0x20` and `0x22` are clones: they share the sample data of `texti1`, `texti2`, `texti3`, `texti4` and `texti5`
  (so five files make up the whole sequence). The counts are the number of times the loader wants a block loaded (see 4.2 for what they
  mean in plays).
* The file `textegn2` (shorter than `textegn`) is a fallback the loader reads only when `textegn` is missing: the shipped disk has both, so `textegn2` is never
  loaded [V: the rig's request list for the three loaders has 13 file names, `textegn2` not among them].
* Object 4 is a 32-byte square wave (16 bytes of +127 followed by 16 bytes of -126, built by `0x55B88`); object 6 is 2,048 bytes of pseudo-random noise
  (section 3.3). Neither has a file.

### 2.3 The three loaders, the memory test and the enable bits [R for the memory test; V: the loaders on the real files, `objects`, 157 checks; the enable longs in `sound-cia`]

| loader | called from | loads | when |
| --- | --- | --- | --- |
| `0x19660` | `0x10456` (start-up) | `text201` as object `0x23`, a clone `0x24`; counts -1; two loader periods; enable bit `V:4AC12` bit 2 | always |
| `0x196BA` | `0x1047E` (start-up) | `texti0a`, `texti0b` and `texti1` to `texti5` and the clones and links of 2.2; counts as 2.2; enable bit `V:4AC13` bit 7 (set only when all of it loaded) | only when the free chip memory (`AvailMem` of the chip type) is above `200,000` bytes at that moment; otherwise there is no music and the `text201` pair is the start-up sound |
| `0x199CA` | `0x11664` (the main program, once, after the graphics set-up) | `textegn` (or `textegn2`), objects 2 and 3 (bit `V:4AC13` 1); the tone, object 4, a 32-byte buffer; the noise buffer, object 6, `0x800` bytes (bit `V:4AC12` 0); `texttre`, object 5 (`V:4AC13` bit 2); the gun clone `0xA` (bit 5, needs object 6); `textger`, object `0xB` (bit 6); the launch clone `0xC` of object 2 (`V:4AC12` bit 1, needs object 2); `textcpt`, objects 0 and 1 (bit 0); `textwnd`, objects 8 and 9 (bit 4) | always (each part when its allocation or file succeeds) |

After a complete load `V:4AC12` = `0x07` and `V:4AC13` = `0xF7` [V: `objects`]. The wrappers test these bits (and that the object exists); a sound whose bit is
clear is skipped, except that the noise wrapper then posts the notice `0xFA` and `V:4A84F` = 2 instead (section 8.2: the notice `0xFA`). The bit `V:4AC13` 5 (the gun) is set
whenever the noise buffer and its clone loaded: with the shipped disk and the memory of the reference machine it is always set (`gun_sound` = 1: the enable longs read `0x000007F7` in the credits snapshot, `sound-cia`; Open questions 2). The music test depends on memory: on a machine with less free chip memory the game plays no music (the port's choice is in section 7).

### 2.4 The playback period of each sample file, for the extractor [C: the rule is chosen; the periods are R and V as stated]

The period an object plays at is a runtime choice (the wrappers and scripts set it, section 5), so a file has no single natural period. **The rule for the listening copies (the WAVs) is: the period of the file's first play in a flight**, because that is what a player hears first and the code and the captures give it exactly. This **overrides the plan's "the object's default period"**: the object field after loading is the loader's 358 for every file object (150 for the gun, 179 and 181 for `text201`), a value that is never heard for the engine, wind, gear or ground files. WAV rate = 3,546,895 / period.

| file | period of its first play in a flight | WAV rate (Hz) | evidence | range afterwards |
| --- | ---: | ---: | --- | --- |
| `texti0a`, `texti0b`, `texti1` to `texti5` | 358 | 9,908 | the object default; the boot capture [V] | fixed |
| `text201` | 179 (voice 0), 181 (voice 1) | 19,815 | the objects' fields; the mission start capture [V] | fixed |
| `textcpt` | 764 (voice 0; 766 for voice 1's object) | 4,643 | the cockpit idle at the flight start: `784 - 18 - 2` for throttle 9 [V: `sound-menu-mode`, `sound-flight-start`] | 514 to 821 (5.5) |
| `textegn` | 776 (voice 0; 778) | 4,571 | the outside idle at the flight start: `800 - 18 - 4 - 2` [V: `sound-flight-start`]; the launch uses it at 140 to 147 | 470 to 834 and beyond (5.5) |
| `textwnd` | 392 (voice 0's object; 422 for voice 1's) | 9,048 | the first draws of the random register after the fill, 32 and 62 [V: 3 captures] | 360 to 423 at each start |
| `texttre` | 310 | 11,442 | the wrapper [V] | constant |
| `textger` | 700 | 5,067 | the script [V] | 700 falling by 7 a blank to 595 |
| `textegn2` | 776 | 4,571 | never loaded | |

`tables/sounds.bin` carries the **loader's** period field, the play counts and the other fields of 2.2, not the WAV rate; the WAV header carries the rate above. The provisional 8 kHz of the extractor is replaced by these (Task 4 shipped 650 and 391 from an earlier draft of this note: it should now carry the table above for `textcpt`, `textegn` and `textwnd`).

### 2.5 What the game does when an object could not be built [R: the wrappers; V: `sound_check.py pass` clears each enable bit in turn, 0 differences; plan Review Focus 2]

The enable bits of 2.3 say what exists; each wrapper has its own fallback. A file that is short is not detected (the object is as long as the file; the derived lengths of objects 3 and 9 are not guarded and can go negative for a file shorter than 880 or 1,938 bytes, which no shipped file is).

| missing | bit clear | what the game does instead |
| --- | --- | --- |
| `textcpt` (objects 0, 1) | `V:4AC13` bit 0 | the cockpit pair wrapper plays the external pair at **a quarter of the volume**; the glide wrapper divides its volume by 4; the cockpit rows of 5.4 test the bit (the afterburner-on row and the "other" rows use the volume `0x28` or `P / 4`) |
| `textegn` (objects 2, 3) | bit 1 | the external pair, the glide and the launch fallback play nothing; with the launch bit also clear (object `0xC` needs object 2) the launch notice is consumed silently |
| the launch clone (object `0xC`) | `V:4AC12` bit 1 | the launch case plays the external pair at `pitch + 0x16` instead (5.4) |
| `textwnd` (objects 8, 9) | bit 4 | the wind wrapper **stops voices 0 and 1**; the flight start plays the external pair at period `0x300`, volume 8 instead of the wind |
| `texttre` (object 5) | bit 2 | ground contacts are silent |
| `textger` (object `0xB`) | bit 6 | the gear key plays the switch beep (7.4) instead of setting the countdown |
| the noise buffer (object 6) | `V:4AC12` bit 0 | every noise wrapper call plays nothing and posts the notice `0xFA` with the hold 2; the gun clone (`0xA`, bit 5) needs object 6, so the gun routine calls the noise wrapper too, and fails the same way |
| the gun clone (object `0xA`) | bit 5 | the gun routine calls the noise wrapper `0x1A0DC(0x1C, 0x30)` |
| the tone buffer (object 4) | none | `0x1A042` plays nothing: every cue, beep and click is silent |
| the music (objects `0xD`-`0x22`) | bit 7 | the start-up phrase plays the `text201` pair at the caller's volume when that exists, else stops all voices; the menu build then plays the pair too; `0x19D7A` is unaffected |
| `text201` (objects `0x23`, `0x24`) | `V:4AC12` bit 2 | `0x19D7A` plays nothing; the start-up phrase without music stops all |

## 3. The scripts and the synthesisers

### 3.1 The script interpreter [R; V: `sound_check.py tone` and `noise`, 312 tone sets and 400 wrapper runs, 0 differences against the model of this section]

Five objects carry a *script*: a list of 8-byte commands (a long field code, a long value) that the per-blank server steps (section 4.3). A voice's object
runs its script when its wait field `+0x2C` counts down to 0 (the server decrements it first); the wrappers start a script by writing wait = 1 and position 0, so the first command
runs in the next blank. The interpreter reads commands until one of them waits:

| field code | command |
| --- | --- |
| `0x08`, `0x0C`, `0x18`, `0x1C`, `0x24`, `0x28` | set the object's period, volume, period step, volume step, loop counter 0 or loop counter 1 to the value (a long, 16.16 for the first four) and read the next command |
| `0x2C` | wait: set the wait field to the value; a value of 0 ends the script (the voice is stopped: its slot is cleared and its interrupt raised, so the handler writes volume 0, period `0x7C` and switches the DMA off); a negative value waits for ever |
| `0x40`, `0x44` | loop on counter 0 or counter 1 to the byte offset in the value: if the counter is 0 jump (a counter of 0 loops for ever); otherwise decrement it and jump if it is still not 0, else go on with the next command |

Each blank, for each voice with an object, the server (4.3) runs the script step, writes the period and volume registers from the object's fields (so a command that
sets a field is seen by the hardware in the same blank), and then adds the steps to the fields.

### 3.2 The tone generator `0x1A042`: the nine parameters [V: 312 parameter sets, 0 differences; `cue_check.py` 310 runs for the cue table]

The tone is object 4 on voice 3: a 32-byte square wave (16 samples +127, 16 samples -126: 16 words, DMA length 16) played in a loop (count -1) under the script `tone`
(12 commands, `0x55CA0`). The routine `0x1A042(p1 .. p9)` first stops voice 3, then writes its nine arguments into the script, sets the position to 0 and the
wait to 1, and starts object 4 on voice 3 (with volume 0; the script sets the volume). With the parameters:

| parameter | meaning | script command it fills |
| --- | --- | --- |
| p1 | first period (hardware period, so the pitch is 3,546,895 / 32 / p1 Hz: the square wave has 32 samples a cycle) | period of command 3 (`p1 << 16`) |
| p2 | first volume (0 to 63) | volume of command 4 (`p2 << 16`) |
| p3 | first duration, blanks (at least 1; 0 would end the script) | wait of command 5 |
| p4 | second period | period of command 6 |
| p5 | second volume | volume of command 7 |
| p6 | second duration, blanks | wait of command 8 |
| p7 | inner repeats of the pair of segments (0: for ever) | loop counter 1 (command 2) |
| p8 | outer repeats (0: for ever) | loop counter 0 (command 1) |
| p9 | final wait after the inner loop, blanks (also the volume stays at p5 during it) | wait of command 10 |

The sequence is: outer repeat p8 times { inner repeat p7 times { (period p1, volume p2) for p3 blanks, then (period p4, volume p5) for p6 blanks }, then wait p9 blanks }, then the
script ends and the voice stops. The tone is therefore a two-segment pattern, repeated, then held; a siren (two periods), a beep (p4 = p1) or a pulse (p5 = 0). **Time:**
the first segment is written in the blank after the call (the script runs when wait 1 expires), the voice stops in the blank
`1 + p8 x (p7 x (p3 + p6) + p9)` blanks after the call (p7 and p8 above 0, p9 at least 1; **p9 = 0 is a wait of 0, which ends the script at once: the voice then
stops after the first outer repeat, at `1 + p7 x (p3 + p6)` blanks** [V: 500 random sets in the model, 0 differences of the formula]); p7 = 0 or p8 = 0 never ends (the alert tones, the lock tone and the "in the
box" tone of note 07 are such: they play until the cue 0x08 or the next tone stops voice 3). The register values: period = p (floor `0x7C`), volume =
min(p2 & 0x3F, master) [V]. The cue table (nine parameters per cue bit, note 07 13.4) fills these.

### 3.3 The noise buffer and the random source [V: `sound_check.py noise`: the 2,048 bytes equal the register sequence below byte for byte; the register after the fill equals the model's]

The game's random source (not the flight's) is a 32-bit shift register in the library (`0x55C9C`, initial value `0x47091247` in the image). One step: the new top bit is
bit 0 xor bit 3 of the register, the register shifts right by one and the new bit enters at the top. `0x55BE4` does one step and returns that bit; `0x55C32(n)` draws
`n` bits (the first drawn is the most significant) and returns them as a number; `0x55C66(buffer, n)` fills `n` bytes with 8-bit draws. The loader fills the noise
buffer (object 6, `0x800` = 2,048 bytes) right after creating it: nothing has drawn before, so **the buffer is the first 16,384 bits of the sequence from the seed, in
bytes** (a signed 8-bit noise whose bytes are the drawn values 0 to 255 read as signed). After the fill the register holds the state the model reproduces; every later
draw (the noise pitch, the wind pitch, the launch pitch) continues from there. **The port uses this register, not the core's generator.** The register is private to the sound library: its only callers are the wind (twice), the noise, the launch and the fill (`0x19FC2`, `0x19FEA`, `0x1A13C`, `0x1A2B4`, `0x55C88`), and no game rule draws from it. It is seeded, so a session is deterministic: the library keeps the register for the whole session, makes the fill at its start (the noise buffer is its first 16,384 bits), and draws in call order with the widths 11 bits (noise pitch), 6 and 6 (wind, voice 0 then voice 1) and 3 (launch). [V: the first two 6-bit draws after the fill are 32 and 62, which give the wind periods 392 and 422 (`0x168` + 32 and + 62) of the first wind of every flight start in `sound-menu-mode`, `sound-flight-start` and `sound-flight-start-m3`: the register's state is continuous from the boot, 3 captures.] The plan's rule that every random value of the sound comes from the core's seeded generator is replaced by this one (determinism is unchanged).

### 3.4 The noise wrapper `0x1A0DC(a, b)` [V: 400 wrapper runs of all kinds, 0 differences]

Object 6 on voice 2, script `noise` (5 commands, `0x55D00`). With `V:4AC12` bit 0 clear (no noise object) nothing plays and the wrapper sets the notice `V:4A968` = `0xFA`
and `V:4A84F` = 2 instead (an overspeed-style message and a short hold, section 8.2). Otherwise it stops voice 2, writes amplitude `a` and duration `b` into the
script, draws 11 bits `r` (0 to 2047), sets the period field to `8,990 + 4r` (`0x231E + 4r`: 8,990 to 17,178; a sample rate of 395 to 206 Hz: a low rumble) and starts the
script. The script: volume = `a` (16.16: `a << 16`) and volume step = `-(a << 16) / b` (integer division toward 0), wait `b` blanks; then volume 0 and the end. The volume
register in blank `k` (k = 1 to `b`; blank 1 is the blank after the call) is the hi word of `a << 16` minus `(k - 1)` steps, masked with `0x3F`; at blank `b + 1` the voice stops.
So `a` is the start volume (0 to 63) and the sound fades linearly to 0 over `b` blanks. The block (2,048 bytes at 8,990 to 17,178 clocks per byte) is 5.2 to 9.9 seconds long, longer than any
duration used, so the noise is never restarted by its own end. The callers' values: (28, 48) the bump and the release, (63, 120) the crash and the ejection bang (section 8).

### 3.5 The other scripted objects [V as 3.4]

| object | wrapper | script (command list) | arguments |
| --- | --- | --- | --- |
| `0xA` the gun, voice 2 | `0x1A2F0(a, b)` (writes only the pulse count, the volume, the position and the wait: **not the period**) | loop counter 0 = `a`; { volume = `b << 16`, volume step 0, wait 2; volume 0, wait 1 } repeated `a` times (a burst of `a` pulses, 3 blanks each: 2 on, 1 off); then period 280, volume 20, volume step -61,440 (-0.9375 a blank), wait 10, end | `a` = 6, `b` = `0x16` (22) in the cockpit view and `0x1E` (30) outside. The pulses play at the object's period field: 150 until a burst has reached its tail, **280 from then on** (a burst cut before its tail leaves the field as it was; the port keeps the object's period across starts). 28 blanks in all [V: the rig's random gun runs; the capture saw 150 only because its held trigger restarted every burst before a tail; Open question 7] |
| `0xB` the gear, voice 2 | `0x1A1E6(v)` | period 700, period step -7 for 15 blanks (then the step is 0: period 595), then wait for ever; the wrapper sets the script (`0x55D28`) and starts the object at volume `v` (the start writes the registers at once with the object's left-over period, 358 the first time and 595 after, for the rest of that blank; the script sets 700 at the next); the end is the stop of voice 2 by `0x1A24C` | `v` = 10 (cockpit) or 5 (outside, `V:4A86D` set) |
| `0x5` the ground contact, voice 2 | `0x1A18C(v)` | no script: the wrapper sets period 310, period step -2 and count 1: one play of the 3,462-byte block (0.30 s at period 310). **The step is never applied**: with count 1 and no next object the handler empties the slot at the DMA-start request, before the first blank, so the server skips the voice; the sound is a constant 310 at volume `v` until the block ends [V: the model and the rig agree on the empty slot and on the absence of register writes in the blanks] | `v` = 15, 30 or 40 |
| `0xC` the missile launch, voice 2 | `0x1A258(v)` | volume 11, volume step +2, wait 8 (the registers show 11, 13, ... 25 in the 8 blanks); volume = `v << 16`, volume step -((v - 1) << 16) / 60, wait 60 (a fade from `v` to 1); volume 0; end. The period is `0x8C + r` with `r` drawn from 3 bits (140 to 147), constant | `v` = the engine volume term of the pass (section 5): 61 in the cockpit |

The gear script's period slide (700 down to 595 over 15 blanks) and the launch's fade are the only sounds with a built-in glide; the engine pair's glides are set by the
wrapper `0x19EFA` (section 5).

## 4. The player, the voice slots, the interrupt handler and the stops

### 4.1 The player `0x19C7C(object, voice, volume)` and the stops [V: every sound_check part]

* **Start.** If the object does not exist nothing happens. Otherwise the voice is stopped first (4.1 below), then the object's volume field is set to `volume << 16`, the object is
  stored in the voice's slot (`0x54ED0 + 4 x voice`) and the voice's interrupt is requested. The interrupt handler (4.2) then loads the registers. The volume the wrappers
  pass is only the *start* volume: a script or a glide replaces it at the next blank.
* **Stop `0x19C58(voice)`.** The slot is cleared and the voice's interrupt requested: the handler finds no object and writes volume 0, period `0x7C` and clears the voice's DMA bit.
* **All stop `0x115F6`.** Stops voices 0, 1, 2 and 3 in that order. Callers: the start-up phrase `0x19CE6` before it starts the music (`0x19D00`) and when it has neither the music nor the `text201` pair (`0x19D70`), the ESC reset
  `0x11AE2`, the crash hand-over `0x12FA0` (cockpit view), the map key `0x1ECE2` (when it switches the map on), the pause key `0x1EDC0` (when it pauses).
* **Voice 2 stop `0x1A24C`** (= stop 2): the end of the gear sound, by the gear countdown (5.1).
* **Voice 3 stop**: the view change (`0x14282`, which every view change calls), the cue 0x08 (silence, `0x14DC6`, note 07). Voices 0 and 1: the flight start
  (`0x12042`, `0x1204E`) and the wind wrapper when the wind bit is clear.
* **A stop and a start keep the counts**: the music restarted after a flight resumes from the counts its objects were left with (4.2).
* **Nothing else stops a sound**: a started object ends by its script, by its count (4.2) or by being replaced.

### 4.2 The audio interrupt handler `0x55158` and the block rule [V: `sound_check.py music`: 120 block ends per voice, voice 0 and voice 1, 0 differences of the loads and the register writes; the tone, noise and pass parts use it at every start]

The handler runs for a voice (a) when the player or a script requests it (the software request, with the slot just set or cleared) and (b) when the hardware raises it: at the start
of a channel's DMA (the first block is under way and the registers may be loaded for the next) and every time a block ends and the next begins. For the voice's slot:

* **No object**: write volume 0, then period `0x7C`, then clear the channel's DMA bit (and acknowledge). The sound has stopped.
* **An object**: write the location (its data address), the length (its length / 2 words), set the channel's DMA bit (together with the master bit) and write the period and volume
  from the object (the writer of 4.3); then, unless the count is -1, decrement the count, and when it falls below 0 put the object's `next` in the slot (0 = the voice is now empty) and
  restore the count from the reload field.

The hardware re-reads the location and length at the end of every block, so what the handler loaded is the *next* block. Counting the loads: the software request loads block 1, the
DMA-start request loads block 2 (block 1 plays now), the end of block 1 loads block 3 (block 2 plays), and so on. **An object with count `c` is loaded `c + 1` times**; because the load for
the next block happens while the current one plays, the effect is: **an object with count `c` followed by another object plays `c + 1` blocks** (then the voice moves on without a gap), and
**the last object of a voice (no next) plays `c` blocks**: when the slot is found empty the DMA is switched off at the moment the already-loaded block would have started, so the last load
is never heard (count 1 = one play: the ground contact, one block of 15.1 blanks with the DMA off in the 16th [V: `sound-gear-touchdown`]; count -1 = for ever). Block durations are `length / 2 x 2 x period` colour clocks (2,048 bytes at 8,990 is 5.2 s). The music (2.2)
therefore plays: voice 0 `0xD` 14 blocks (2.9 s), `0xF` 2 (0.4 s), then the loop of `0x11` 3 blocks, `0x13` 1, `0x15` 3, `0x17` 1, `0x19` 3, `0x1B` 1, `0x1D` 3, `0x1F` 1, `0x21` 4 (79.5 s) for
ever; voice 1 the same with the odd objects, started in the same blank, so the two voices play the same data in step: **the music is the same mono signal on both sides**.

**The capture agrees with the rule** [V: `sound-boot`, the cold boot stepped for 8,200 frames, voice 0's length register sampled once per frame; `sound_capture_check.py chain`]. The music starts 4,259 frames (85 s) after power-on (the
software load at the start-up routine, the DMA bit set); the loads of a different sample then happen at the frames 4,393 (`texti0b`), 4,414 (`texti1`), 4,754 (`texti2`), 4,919, 5,414, 5,580, 6,076, 6,242, 6,739, 6,905 (`texti5`), 8,061
(back to `texti1`): **12 changes, each within 0.7 frame of the frames the rule predicts** (a block start loads the next block, block `j` lasts `length x period` colour clocks, the object chain of 2.2; the first
object loaded 14 times, the second twice, `0x11` three times, `0x21` four times, 0 differences). Note the shift in what a change of the loaded sample shows: the interval between the first load of one object and the first load of the next
is the *previous* object's last block plus the object's own further blocks, because a load is made one block ahead of the sound.

**The blank of the game's display is 313 lines** (71,051 colour clocks, 49.92 blanks a second, not 50.00): the block times above fit only with that length (a 312-line frame gives 4 to 12 frames of drift over the music; the
recorder's 883.4 samples per frame at 44,100 Hz say the same). All the blank counts of this note are in these blanks. A block lasts its length x period colour clocks exactly; the port's library counts the 71,051 clocks of a blank for block durations (9.2) and runs 6.25 blanks per simulation step, i.e. 50.0 blanks a second, 0.16 % faster than the machine's 49.92 (the mixer consumes the blanks at the audio device's rate and skips whole blanks of output to shed the surplus of a long run and never merges two blanks, never drops or reorders a latch or a DMA write: rule (d) of 9.2 [C]).

The hardware's own repeat is not used for the looped sounds either: a count of -1 means the handler reloads the same object at every block end (the engine loop, the tone, the noise).

### 4.3 The per-blank server `0x551D8` and the register writer `0x55260` [V: every blank of every part, write by write]

Once per blank, for voice 0 to 3 in order, if the slot holds an object: (1) the script step (3.1: decrement the wait; run the script when it reaches 0), (2) write the registers: period =
the hi word of the object's period field (at least `0x7C`), then volume = the hi word of the object's volume masked with `0x3F`, limited to the master level's hi word (section 6),
(3) add the period step to the period field and the volume step to the volume field, (4) if the period timer is set, decrement it and when it reaches 0 set the period step to 0; the
same for the volume timer. The registers are therefore written *before* the steps of the blank are added: a field set by a wrapper at pass time is written at once by the handler,
and the first step is added at the next blank. **The port** (9.2) makes a wrapper's register writes at the pass, before the blank loop of that step, which is at most one blank earlier than the original's mid-blank pass; scripts wait for the blank as in the original [C]. A voice with no object is skipped (nothing is written).

### 4.4 Interrupt, start and stop traffic of a start [V: the rig's register log of every start, `tone` 312 sets and `noise` 400 runs; the watch-mode capture `sound-click` shows the same order]

A wrapper start writes, in this order for the voice: `INTREQ` (request), then the handler with no object (volume 0, period `0x7C`, DMA clear), the same for the player's own stop, then the request
with the object (location, length, DMA set, period, volume) and the DMA-start request (the same writes again, the count one lower). The port needs none of the interrupt traffic: it needs
the voice's object, the block counts of 4.2, and the period and volume of every blank.

## 5. The per-pass routine `0x14AA0`: engine, wind, afterburner, gear and the cue player

The routine is called once per pass by the frame routine (note 07, 13.x: after the alert arbiter, before the panel and HUD routines). Let `R` be the *viewed record*: `0x4B23C + 512 x V:4A994`
(the camera's record: the player's, or the ejection seat's, note 05), `W2` the word at `R + 2` (bit `0x0008`: afterburner on, bit `0x0080`: wheels down, note 05), `T` the signed
throttle byte `R + 0x2B` (-32 to 120), `W` the indicated-speed word `R + 0x6E` (twelfths of a knot, note 05), `E` the pitch-rate word `R + 0x56`, `Q` the roll-rate word `R + 0x5A`,
`class` the byte `R + 0x62`, `N` the notice byte `V:4A968`, `view` the byte `V:4A83D` (0 cockpit; non-zero outside: -1, i.e. `0xFF`, is the default outside view and 1 to 4 the further levels, note 06) and `H` the byte `V:4A84F` (the hold counter). All
arithmetic is on 16-bit words with arithmetic shifts. [V: `sound_check.py pass`: the model of this section against the real routine and the real library, 2,000 passes with six blanks after each (12,000
blanks), every register write compared, 0 differences; 30,000 passes in a longer run, also 0; the random states cover the gates, the notice values, the views, the records of class `0x3x`, the
enable bits cleared one at a time, and the calls (`gear` 510, `stop2` 172, `wind` 535, `eng_ext` 494, `eng_cockpit` 68, `glide` 73, `glide_b` 30, `launch` 116, `noise` 469 in the 2,000).]

**Captures** [V: `sound_capture_check.py engine`, the logs of `sound-throttle`, `-afterburner`, `-views`, `-eject-ext`, `-crash`, `-gear-touchdown`, `-gun-launch`, `-gear`, `-cue`; the inputs (the viewed record, the globals, the enable longs) peeked every 6 blanks]: of the samples in
which voice 0's period and volume had been the same for 12 blanks, **810 of 811 equal the model's glide target (within one unit; the last unit stays open because the glide's division truncates) for the inputs of that sample or of one
of the 8 samples before it**; the others are retargets not yet made (the engine is retargeted every 4th pass, so a sample can be settled at the old target while the inputs have already changed). Single points that pin the
formulas: the idle engine at the flight start (throttle 9, cockpit): periods 764 and 766, volume 40 (`0x310 - 18 - 2`, and the floor `0x28` over 36); full military power (120): 514 and 516, volume 63; the afterburner switching on (`0xFD`): the
external pair at 549 (= 514 + `0x23`) and volume 30, then falling by the glide to the afterburner level; switching off (`0xFC`): the cockpit pair at 549 (= 517 + `0x20`) and volume 63; throttle 0 in the air: the wind at volume 1 (periods 407 and 414 in one start: `0x168` + 47 and + 54), and the engine back at the next throttle key.

### 5.1 Gates, the gear countdown, the cue player, the engine gate [V: the 2,000 passes of section 5 for items 1, 2, 4 and 5; the tick and the order of item 7 are R]

1. **Gates.** Nothing at all happens (and `N` is not cleared) when `V:4A966` is non-zero (the flow states: credits, selection screens, the hold after a crash), when `V:4A865` (the map
   view byte) or `V:4A866` (the pause byte) is non-zero, or when `V:4A84D` (the game gate) is 0 [V].
2. **The gear countdown `V:4A93D`.** If bit 7 is set (the gear key `0x1E966` sets `0x83` when the gear sound exists, `V:4AC13` bit 6): in the cockpit view the gear sound starts at volume 10; in
   an external view at volume 5 when `V:4A86D` is set (the flight value) and not at all otherwise; the bit is cleared (the byte is now 3). Otherwise, if the byte is 1 the gear sound stops
   (`0x1A24C`, voice 2) and the byte becomes 0; other values are left. The byte is counted down by the clock routine (`0x29882`, inside the frame limiter `0x297D6`: it runs in a drawn frame only, so not in a
   pause): **once per change of the system clock's seconds long `V:4ABAA`, that is once per second** (the same tick counts the cue cool-down `V:4A942`, the head-hold byte `V:4A949` and `V:4A93E`, and
   drives the blackout accumulator, notes 04 and 06); it clears bit 7 first, then decrements, not below 0. So the gear sound starts in the pass of the key (byte 0x83, then 3), the first tick
   makes it 2, the second 1, and the next pass stops it: **it lasts 1 to 2 seconds plus up to one pass**, the object's loop of 0.4 s repeating at the final period after the 15-blank slide
   [V: the wrapper calls and the byte in 2,000 passes; the tick by the capture `sound7-tick` (milestone 7: 120 s, 6,000 frames sampled every 4: V:4ABAA changed 119 times, 49.97 frames a second, the clock of the
   timer device counting 50 a second; the gear byte (4 decrements in two countdowns), the head hold (5, the last from 1 to 0xFF) and the cool-down of the cue 0x01 (4, the last from 0 to 0xFF): **all thirteen decrements fell in the 4-frame sample in
   which V:4ABAA changed, and no other sample held one** (the stores by the key, the poke and the cue are the other changes; the gear byte's stop, 4 frames after its last decrement, is the next pass). The tick has no
   phase against the mission: the microsecond long V:4ABAE read at the passes that saw a change ran from 2,074 to 124,264, the clock being the machine's; a second is 50 blanks, its phase is arbitrary and not a rule]
   **The gear sound by camera** [V: `sound7-fixedcam`, `sound7-seat`]: in the fixed camera (SHIFT and KP-ENTER: the view byte 0xFF, V:4A86D clear, the camera V:4A994 = V:4A996 = 0, the player's record) the
   byte 0x83 poked was cleared and counted down (3, 2, 1, 0, one a second) and **no gear sound started** (120 frames; this is the "not at all" of the rule above, the outside volume with V:4A86D clear); back in the ring
   camera in the cockpit view the same poke started the sound (period 700 falling by 7 a blank to 595, volume 10, 98 blanks of voice 2). With the camera on the ejection seat (SHIFT-E, V:4A994 = 1, V:4A996 = 0x200,
   view byte 0) the poke started it too: period 700 falling to 595, **volume 10**, playing for at least 53 blanks (the log has a gap after that, where the machine ran under a stop request). So **the wrapper's camera test (V:4AA6E = V:4A996, 5.7 item 12) passes for the seat in the sound pass**.
3. **The cue player and the bang** (the cue tones: note 07 13.4, `V:4AC0C`, cool-down `V:4A942`; run between the gear block and the engine block, [V: `cue_check.py`, 310 runs]). The bang's countdown `V:4A870` (a signed byte) follows in every
   pass that passed the gates of item 1 (also in the passes whose engine block is skipped): a non-zero byte is decremented; **when the result is 0 or negative the bang plays and the byte is cleared**:
   `0x1A0DC(V:4A922, V:4A921)` outside, `0x1A0DC(V:4A922 >> 1, V:4A921)` (arithmetic shift of the signed byte) when `V:4A83D` = 0, 8.2. Writers: the ejection seat's step sets `V:4A922` = `0x3F`, `V:4A921` = `0x78` and `V:4A870` = `0xFF` at the nibble-1 step (`0x26FC0`-`0x26FD0`),
   which is -2 after the decrement, so **the bang plays in the same pass** when the seat's step precedes the sound routine (it does) and never after 255 passes; the entity drawer sets `V:4A921` = d, `V:4A922` = d / 2 and `V:4A870` = 4 - d / 32 (as a signed word, stored as a byte) for a dying record drawn with its counter at 14 (8.2). A later writer overwrites the arguments and the countdown of an earlier one before it
   has fired [V: `sound7-bang-overwrite`: the three bytes poked (0x20, 0x50, countdown 2) played a bang of first volume 16 (0x20 >> 1) for 80 blanks; with (0x20, 0x50, 4) poked and, one pass later (the countdown reading
   3), (0x30, 0x60, 4), the bang that played was **(0x30, 0x60): first volume 24, 96 blanks**, the earlier arguments gone; the length is the byte V:4A921 in blanks, the volume falls to 0 at that blank]. If the noise object is absent the wrapper posts `0xFA` and the hold 2, and the same pass's engine block already sees the hold [V: `sound_check.py pass` models the countdown, the bang, the overwrite-free sequences and the failed wrapper: 30,000 passes with `V:4A870` drawn from 0, `0xFF`, 1 to 4, 0 differences; the first volume of the two ejection bangs V in the captures].
4. **The engine gate.** If `N` is 0 the engine block is skipped except in every fourth pass: it runs when the pass counter `V:4A879` has its two low bits 0. A non-zero notice always runs it. So with no
   event the engine is re-targeted every fourth pass (about every 0.5 s of play at 8 passes a second), each time as a 32-blank glide (below) [V].
5. **The hold counter, in every ungated pass** (the skip of item 4 jumps to it): when `H` is above 0 (signed byte) it falls by one; when it reaches 0 in the cockpit view with the afterburner off (`W2` bit 3 clear) the notice is set to `0xFE`. **The notice is cleared (`N` = 0) only when the engine block ran**; in a skipped pass it is left as it is (it is 0 there, unless the failed noise wrapper or this item posts it) [V].
6. **Signed bytes**: `V:4A941`, `V:4A84F`, `V:4A942` and `V:4A96A` are compared as signed bytes (`V:4A941` is -1 when idle); `V:4ABFA` is a signed word in a signed 32-bit division that truncates toward 0; `V:4A83D` is 0 in the cockpit, -1 (`0xFF`) in the default outside view and 1 to 4 for the further levels; `T` is a signed byte.
7. **Order inside a pass** [R: `0x11124`, the call sites `0x1158A`-`0x115A6` (key poll), `0x11282` (this routine), `0x11446` (the limiter `0x297D6` and with it the clock routine `0x29882`)]: the key poll first (the gear key sets `V:4A93D` = `0x83`), the sound routine later in the pass, the seconds tick last. A gear key is therefore seen by the sound routine of its own pass, before that pass's tick could clear bit 7; the "swallow" (the tick clearing bit 7 before any pass sees it) would need the key handled after the tick, which no path does
   [V: `sound7-swallow` (milestone 7): the gear byte poked (the key body's store) 16, 14, ..., 2 and 0 frames before the pass that sees a new seconds long, nine trials four seconds apart. In the eight trials with the poke before the sound routine
   of the pass the gear sound started 2 to 6 frames later; **in the two of them whose pass was also the tick's (the first sample after the poke already read 2) the sound started all the same**: the sound routine ran before the tick, and the tick cleared
   bit 7 and counted 3 to 2 after it. In the ninth trial (the poke 0 frames before the change) the byte went from 0x83 to 2 within 4 frames and **no sound started**: the poke fell between the sound routine and the tick of that pass, and the tick
   cleared bit 7 first and decremented, the "swallow". A key press cannot do that: the key poll that stores 0x83 runs at the head of the pass, before the sound routine].  A key pressed during a pause sets the byte (note 06) and the sound starts in the first pass after the unpause (the tick does not run in a pause). The same routine is called a second time at `0x114C0` in the passes that do not run the flight path (no engine: its gates are `V:4A84D`). The core already keeps the clock second as `game_clock_second` (note 06 head-hold rule): the gear byte, the cool-down and the head hold count it.

### 5.2 Which sound: the engine, the wind or nothing

All words are 16-bit, a quarter or half of a value is rounded toward minus infinity (an arithmetic shift), `|x|` is the absolute value.
If `class` has the high nibble 3 (the record is an ejection seat or a similar object, note 05) the engine block is only the wind at volume `0x10`. Otherwise let `a0 = |T|` and compute the engine volume `P0` (5.3). If `P0` is 0 the block is only the wind (5.6).
Otherwise let `w = |E / 16|` and `u = |Q / 16|`; when `u` is less than `w`, add `E / 16` (signed, so it can subtract) to `u`. The throttle term is `a = a0 - u` when `a0` is 120 or more, else the smaller of `a0 + u` and 120 [V]. **`a` is not bounded below**: `u` is negative when
the pitch rate is nose-down and larger than the roll rate, and at full throttle a turn (`u` positive) lowers `a`; the formulas are verified over full-range 16-bit rates, the ranges of 5.5 hold for `a` from 0 to 120.

* **Pitch** (the larger the period, the lower the sound): outside `b = 800 - 2a - a/2 - a/4`; cockpit `b = 784 - 2a - a/4` (a/2 and a/4 rounded down).
* **Volume**: from `P0`; outside `P` = `P0` less a quarter of `P0`; in the cockpit `P` = the larger of `P0` and `0x28`.

### 5.3 The engine volume `P0` of the throttle magnitude `a0` (`0x1530E`) [V: the 2,000 passes of 5; the idle and military values in the captures]

1. `a0 = 0`: `P0 = 0x0C` when `W2` bit 7 is set (wheels down) and `W` is not 0 (rolling), else 0. **The routine returns here**: none of the terms below apply to this value (the floor and the reduction of 5.2 still do).
2. Otherwise let `x` be: 63 when `H` is not 0; else for `a0` above 8, `63 - (120 - a0) / 4`; else `60 - (120 - a0) / 2`.
3. Cockpit view: when `V:4A941` (signed byte, the blackout accumulator of note 04) is above 5, `x` is halved. `P0 = x`.
4. Outside view: with the in-flight byte `V:4A86D` set, let `s` be the signed direction byte `V:4A96A` (0 front, 2 right, 4 behind, 6 left, 8 above, 9: note 06); when `s` is above 4 it becomes `8 - s`, not below 0; `s` = 0 or 1: `x` becomes `x / 2 + x / 8`; `s` = 2: `x` less a quarter of itself; `s` = 3: less an eighth; `s` = 4: unchanged. Then by the level `view - 1`: 0: `x` less an eighth; 1: less a quarter; 2: `x / 2 + x / 8`; 3: half; the default outside view (-1) and larger levels: unchanged.
   With `V:4A86D` clear and `V:4ABFA` non-zero instead: `x = 32 x / V:4ABFA` (signed division), at most `0x54` (the views that clear `V:4A86D`: the fixed camera and its toggle `0x1EBD4`, `0x1E48E`; `V:4ABFA` is stored by the queue-C walker for the viewed record in an outside view and is `0x7FFF` in the cockpit, note 04a 8a). Finally, when `W2` bit 3 is clear (afterburner off) `x` is reduced by a quarter, and `P0` is the result, at least 1.

### 5.4 What is played: the notice byte `N` [V: the 2,000 passes of 5 (every table row and wrapper); the posting sites R]

The notice is posted by the flight code (instruction addresses): **`0xFE`** (restart or retarget the engine) by the restart after a crash hold `0x1308C`, the flight-start wait state `0x139DC`, the view setter `0x1423E` (every view change),
the throttle step `0x15FE8`, the throttle key from idle `0x1624C`, the landing `0x16FE2`, the player start `0x1D564`, the seat view `0x1EC24`, the pause release `0x1EDAC`, and the training instructor `0x272D2`;
**`0xFD`** the afterburner switching on (with `H` = 6, `0x16054`); **`0xFC`** it switching off (`0x160E0`); **`0xFB`** a launch by the camera's record (with `H` = 8, `0x27660`-`0x27668`);
**`0xFA`** the overspeed message (700 knots; with `H` = 2, `0x16846`) and the failed noise wrapper (`0x1A178`, with `H` = 2); **`0xFF`** a direction key of the external view (`0x1E510`, `0x1E5A4`); a value not named in the table takes the "other" rows [R; V: the pass part posts each of `0xFA`-`0xFE` and the model agrees]. With the throttle term, the pitch `b` and the volume terms `P` of 5.2 and `v6` (= `0x3D`; outside with `V:4A86D` clear and `V:4ABFA` non-zero:
`min(0x3F, 0x3D x 32 / V:4ABFA)`):

| view | N | call (wrapper, objects, voices) | pitch | volume |
| --- | --- | --- | --- | --- |
| outside | `0xFE` | `0x19E46`: the external pair, objects 2 and 3, voices 0 and 1 | `b` | `P` |
| outside | `0xFD` | `0x19E46` | `b + 0x20` | `v6` |
| outside | `0xFC` | `0x19E46` | `b + 0x20` | `P` |
| outside | `0xFB` | `0x152C6`: the launch (object `0xC`, voice 2), or the external pair at `b + 0x16` if the launch bit is clear | `b` | `v6` |
| outside | `0xFA` | `0x19E46` | `b + 0x16` | `min(2 x v6, 0x3F)` |
| outside | other (0, `0xFF`) | `0x19EFA`: glide of the objects now on voices 0 and 1 | `b` | `P`, over 32 blanks |
| cockpit | `0xFE`, afterburner off | `0x19DB2`: the cockpit pair, objects 0 and 1, voices 0 and 1 (the external pair at `P >> 2` if `V:4AC13` bit 0 is clear) | `b` | `P` |
| cockpit | `0xFE`, afterburner on | `0x19E46` | `b` | `P >> 2` |
| cockpit | `0xFD` | `0x19E46` | `b + 0x23` | `0x1E` |
| cockpit | `0xFC` | `0x19DB2` | `b + 0x20` | `P` |
| cockpit | `0xFB` | `0x152C6` (launch, volume `0x3D`) | `b` | `0x3D` |
| cockpit | `0xFA` | `0x19E46` | `b + 0x16` | `0x28` |
| cockpit | other, `H` > 0 | `0x19EBE`: glide | `b` | `0x0A` (the call passes `0x0A`, or `0x28` which `0x19EBE` divides by 4 when the cockpit pair is absent), 32 blanks |
| cockpit | other, `H` = 0, afterburner off (or no cockpit pair) | `0x19EBE` (`P >> 2` if the cockpit pair is absent) | `b` | `P`, 32 blanks |
| cockpit | other, `H` = 0, afterburner on | `0x19EBE` | `b` | `(P >> 2) + (P >> 3)`, 32 blanks |

The wrappers: **`0x19DB2(pitch, volume)`**: with `V:4AC13` bit 0 and object 1 present, clear the steps of objects 0 and 1, set object 0's period to `pitch << 16` and object 1's to `(pitch + 2) << 16`, and
start object 0 on voice 0 and object 1 on voice 1 at `volume` (each start stops what the voice played); without bit 0 it calls `0x19E46` with `volume >> 2`. **`0x19E46`**: the same with objects 2 and 3, needing `V:4AC13`
bit 1. **`0x19EFA(pitch, volume, ticks)`** (called as `0x19EBE`, which divides the volume by 4 when bit 0 is clear): needs `V:4AC13` bit 1 and a non-empty voice 1; sets on both objects now in voices 0 and 1 the period step
`(pitch << 16 - period of voice 0's object) / ticks` and the volume step `(volume << 16 - volume of voice 0's object) / ticks` (integer division toward 0), with both timers = `ticks`: the steps are added once per blank
and cleared after `ticks` blanks. **`0x152C6(pitch, volume)`**: with `V:4AC12` bit 1 it calls the launch wrapper `0x1A258(volume)` (3.5) and clears `H`; without it the external pair at `pitch + 0x16`, `volume`. `0x19EFA` reads voice 0's object without testing it (only voice 1 is tested): voices 0 and 1 are always set and cleared together, so the case does not arise and a port needs no other guard. The wind start (5.6) does not clear the steps or timers of objects 8 and 9, so the rest of a glide goes on across a new wind start for up to 32 blanks (a rolling aircraft at throttle 0 glides whatever voices 0 and 1 hold). The detune of
voice 1 is +2 periods (a slow beat between the two engine voices). The volume and pitch values are written as the 16.16 fields; the registers see their hi words [V].

### 5.5 The value ranges, for the port [V: the random states of the 2,000 passes]

| quantity | range |
| --- | --- |
| pitch period outside, `0 <= a <= 120` | 470 to 800; with the notice offsets `+0x20` (`0xFD`, `0xFC`), `+0x16` (`0xFA`, the launch fallback) up to 832; voice 1 `+2`: 834 |
| pitch period cockpit, `0 <= a <= 120` | 514 to 784; `+0x20` (`0xFC`) up to 816, `+0x23` (`0xFD`, the external pair played from the cockpit) up to 819, `+0x16` (`0xFA`) up to 806; voice 1 `+2`: 821 |
| pitch period for `a` below 0 or above 120 | beyond these (the term `u` of 5.2 is not bounded; it follows the rates, note 05) |
| engine volume `P` | 1 to 63 (outside at most 63 - 15 = 48 after the reduction; the cap `0x54` of 5.3 reaches at most 63 after the reductions); cockpit `0x28` to 63 |
| register volume | 0 to 63, limited by the master level (section 6); the library masks with `0x3F`, which only matters for a volume of 64 or more, which the callers never pass |

### 5.6 The wind `0x19F9A(volume)` [V: 535 wind calls in the 2,000 passes and the wind runs of `noise`; the captured wind starts]

With `V:4AC13` bit 4 clear it stops voices 0 and 1. Otherwise (object 9 present) the periods of objects 8 and 9 are set to `0x168 + r1` and `0x168 + r2` with `r1`, `r2` two 6-bit draws (in that order) and objects 8 and 9
start on voices 0 and 1 at `volume`. The volume of the wind is `0x10` for a record of class `0x3x`, else `|W| >> 9` limited to 63, halved in the cockpit view (a record in the cockpit sees the wind at half volume),
and at least 1. The wind replaces the engine on voices 0 and 1 whenever the throttle term is 0 (engine off or at rest on the ground).

### 5.7 Behaviours the model depends on (stated after the core implementation ran the original)

1. **A script that ends a voice does not end the blank's work for it** [V: the model reproduces the rig's register log over all parts]. When the script step clears the voice's slot (a wait of 0), the server has already taken the object: it still writes the object's period and volume registers and adds its steps in the same blank, after the stop's own writes (volume 0, period `0x7C`, DMA off). The last register values of that blank are therefore the old period and volume with the DMA off; only the DMA bit matters to a mixer.
2. **The engine gate reads the notice once**, into a local copy at the head of the pass [R; V: the 30,000 passes, including a failed noise wrapper that posts `0xFA` in the same pass]. The gate test (`N` zero and the pass counter's low bits) and every case of 5.4 use that copy; a notice posted during the pass (by the failed noise wrapper) is not seen until the next pass, and the clear at the end of the engine block wipes it (it survives only in a skipped pass).
3. **The cockpit "other" rows test `H` for non-zero**, not for above zero [R: `0x151B0`]; the decrement of 5.1 item 5 and the volume test of 5.3 use the signed byte (`H` is never negative in play: its writers store 2, 6, 8 and 0).
4. **A negative external-direction byte `V:4A96A` matches no direction case** [R: the case switch of 5.3 step 4 is an unsigned comparison; the byte is only brought into range when it is above 4]: `x` is left as it is by the direction step (the level step still applies).
5. **The cue player clears the whole long `V:4AC0C` whenever it runs past its tests**, also when only the bit `0x200` (the roll-out, tested by nothing) is set: that bit is lost without a tone [V: `cue_check.py`, the 300 random combinations include it].
6. **The gun wrapper `0x1A2F0` takes no enable bit**: it needs only the object `0xA`. The gun routine chooses (`0x17480`): bit 5 of `V:4AC13` set calls the gun wrapper, clear calls the noise wrapper `0x1A0DC(0x1C, 0x30)`; the bit is set exactly when object `0xA` exists, so the two tests agree [R].
7. **The launch wrapper's slope is a signed division** (`-((v - 1) << 16) / 60`, truncated toward 0, like every division of the library: the glide steps, the noise slope) [V: the launch runs in `noise`, 4 captured launches].
8. **The second call of the sound routine at `0x114C0`** is made when the frame routine finds the game gate `V:4A84D` clear (`0x11146`). The routine's own first test is that same gate, so the call changes nothing but the global `V:1A6C0` (the viewed-record pointer it stores at its head): no sound is started or stopped, no countdown runs, `N` and `H` are untouched [R; V by `sound7-approach`: the 77 entries with V:4A84D clear changed none of the bytes read].
9. **`V:4ABFA`** (the camera distance word) is stored by the entity walker for the camera's record: `0x7FFF` in the cockpit view, the record's distance word in the outside views (note 04a 8a). The sound routine reads it only with `V:4A86D` clear in an outside view (5.3 step 4, and the volume `v6` of 5.4). That state is **reachable in flight**: the fixed camera and the toggle `0x1EBD4` clear `V:4A86D` (note 06). It is *not* reached in the mission-start approach: the approach passes are gated by `V:4A966` = 6 and play nothing (item 10). The value is observable in those views, so it must be kept (`0x7FFF` gives `v6` = 0 and `x` near 0; 0 would skip the term). [R]
10. **The approach before a flight is silent and keeps its notice** [V: `sound-flight-start`, `-m3`]. `0x12C90` is the state that **starts the approach** (it sets `V:4A966` = 6, plays the wind or engine call of 7.2 and enters the approach state `0x12D58`); while `V:4A966` is non-zero the whole routine is gated (5.1 item 1), so the notice `0xFE` posted by the flight-start code stays pending (the capture reads `N` = `0xFE` for the 1,200 frames of the approach) and the wind of the dispatch plays on (volume 8, periods 392/422). The first ungated pass (`V:4A966` back to 0) runs the engine block with that `0xFE` and the **view of that moment, which is still the outside view** (`V:4A83D` = 1, `V:4A86D` = 1): `a0` = 9, `P0` = 15 and `P` = 12 (below), pitch `800 - 18 - 4 - 2` = 776, so the **external pair latches at 776/778, volume 12** (captured, 10 blanks before the view change); the view switch to the cockpit lands **in the same frame** (its view setter stops voice 3 and posts `0xFE` again, but the sound routine of that pass has already run), and the next pass, about 10 blanks later, latches the cockpit pair at 764/766, volume 40. The gate is opened by the approach camera's routine (`0x2DC00`-`0x2DC5A`: when the camera is within `0x240` of the aircraft it counts a small counter down and then sets `V:4A97E` bit 1 and clears `V:4A966` at `0x2DC54`) while the view is still outside; a pass lasts about 13 blanks in the approach [R for the camera routine; V for the frames: capture 10534]. The volume 12: `x` = 63 - 111/4 = 36; `V:4A86D` set, direction 0: `x/2 + x/8` = 22; level `view - 1` = 0: less an eighth = 20; afterburner off: less a quarter = 15 (= `P0`); outside `P` = 15 - 3 = 12. **The routine's entry in every pass of a mission start** [V: `sound7-approach` (milestone 7: mode 3 from the aircraft key, a stop at the routine's entry `0x14AA0` in 210 successive passes, the gates and the view read at each)]: the routine is entered in every pass, the approach's included, **and returns at its first test in all 125 passes of the approach (V:4A966 = 6, V:4A84D = 1, the notice 0xFE pending throughout, V:4ABFA moving from 0x7FFF through 0x59 to 1 as the approach camera comes in: never read)**; the 126th entry has the gate open (V:4A966 = 0) with the outside view (V:4A83D = 1) and the notice 0xFE: the first ungated pass of this item; the 127th entry sees the cockpit view (V:4A83D = 0, V:4ABFA = 0x7FFF) and the notice 0xFE posted again by the view setter; then six passes more with the gate and V:4A84D open, the notice consumed (0) and the engine retargets every 4th pass (**2 + 6 ungated passes**, the "2 + 6" of note 08 12.14); then V:4A84D = 0 (the typed notice: 77 further entries, the pass counter V:4A992 standing at 0xA7, nothing runs). So the sound routine does not run in the approach cameras at all, and V:4ABFA (which only a fixed camera, V:4A86D clear, reads) is irrelevant there. A port that starts the engine only at the cockpit view, or glides the wind, differs: it must keep the notice through the gated approach and play the two latches in this order. The pass counter `V:4A879` has no fixed phase there (124 and 63 at the dispatch in the two captures: it counts passes from the mission initialisation and is not reset by the flight start); the first update is notice-driven, the later every-4th updates follow the counter.
11. **The bang fires in the pass that makes the countdown reach 0.** Both writers precede the sound routine in the frame (the seat's step in the world update, the explosion in the entity pass), so a countdown of 1 (explosion) or `0xFF` (seat) is decremented to 0 or -2 and fires **in the same pass**; only a writer that runs after the sound routine in a pass fires in the next. [R]
12. **`V:4AA6E` in the camera test of the wrappers.** The ground wrappers run inside the player's step, where `V:4AA6E` = 0 (note 05 table; the stepped record's offset), so the test is true when the camera is the player (`V:4A996` = 0). The gear wrapper runs in the sound pass, where `V:4AA6E` holds the offset last stored by the record loops of that pass (the walker stores each drawn item's offset: `0x1FC16`, `0x1FE86`); the test passed in all five gear starts of the captures (cockpit, outside, the demo) **and in the sixth, with the camera on the ejection seat (`sound7-seat`: V:4A994 = 1, V:4A996 = 0x200, the gear sound at
   volume 10)** [V]. The fixed camera has V:4A994 = V:4A996 = 0 (the player's record, `sound7-fixedcam`, 5.1 item 2) and never reaches the gear wrapper (V:4A86D clear in an outside view: no start) [V]. **The rule for the
   port: in the sound pass the wrapper's camera test is true for every camera, the seat included** (the walkers store the viewed record's offset last, so V:4AA6E equals V:4A996 in the pass); the player's step is the
   other place, where V:4AA6E = 0 and the ground wrappers (touchdown, nose settling) need the camera to be the player [R: the seat's own contacts were not captured; they are the seat's, not the player's, step].
13. **Click requests along a typed line.** Every request is a new tone call, which stops voice 3 and restarts its script: requests closer than the click's 4 blanks cut the earlier click (the later one runs from its blank 1). On the text screens about 2.4 to 6 passes happen per blank (note 08 3.8), so a typed line's requests are spread over blanks; `sound-click` shows clicks of 4 blanks when the requests are 4 or more blanks apart and a click every blank (each cut after one blank) when they come every blank (the prompt typing in `sound-menu-mode`). In the pause screen the clicks are 2 blanks apart, a character each, the first two 3 blanks apart (`sound7-pause`: nine starts at +13, +16, +18, ..., +30 blanks after the key's stop [V]; 6 passes a character, note 08 5.14; a tenth start at +34 is a NOTE-GAP the port does not make); the menu build of a flight's end (`sound7-restart`) types its list 33 blanks after the stop-all: the 19 instant-line starts one a blank from k = 46, then the typed characters about every 6 to 7 blanks [V: 30 click starts in `sound7_check.py restart`]. **A click and a wrapper call in one step** [V: `sound7-restart`, the per-write trace]: after SHIFT-ESC from the air the stop of all four voices and the music restart come **11 blanks after the key with no click before them** (the first reading took the sound interrupt's rewrites of voices 0 and 1 for 11 blanks of clicks, see 7.2); the first click of voice 3 falls 33 blanks after the stop, then the instant-line starts and the typed characters. The wrapper's writes come after the clicks of the same text, never before: the order the NOTE-GAP of `sound.c` assumes for a wrapper called in the step of a click (stamped at the blank the step has reached) is the original's [V for the order; the exact blank is the pass cost model, C]. The port makes no click before the stop and stops in the key's step (the deviation list of the milestone-7 report).
   The port must give each request the **blank of its pass** (the pass cost model of note 08 3.8 gives the position: cumulative cost / 6,000 units per blank) and call the library at that blank, not all at blank 0 of the step. [V for the original's behaviour; C for the mapping]

## 6. The master volume and its ramp `0x294AC`

### 6.1 The rule [V: `sound_check.py ramp`, 2,000 calls with random flag, target and level, 0 differences; the capture `sound-menu` (6.3)]

The game's own vertical-blank server calls `0x294AC` once per blank. Its three cells: the **level** `V:54FBE` (a 16.16 long between 0 and `0x3F0000`), the **target** `V:4A720` and the
**flag** `V:4A88F`. When the flag is 0 nothing happens. Otherwise, if the level equals the target nothing happens; if the level is below the target it rises by `0x4000`, but not above
`0x3F0000`; if it is above the target it falls by `0x4000`, but not below 0. The step is not clamped to the target (a level a fraction of a step above the target goes below it), but every
target the game sets (0, `0x1F0000`, `0x3F0000`) is a whole number of steps from the levels it starts at (`0x200000` = 128 steps from `0x3F0000` to `0x1F0000`). The level's hi word
caps every voice volume (the writer of 4.3): **a ramp step of `0x4000` is a quarter of a volume unit per blank**, so the cap falls by one unit every four blanks: after `k` blanks of a ramp from
`0x3F0000` down the cap is `63 - ceil(k / 4)`.

### 6.2 Who sets the cells [R: each store read in the code; V for the credits key and the mode key rows (6.3) and for the menu build after a flight (`sound7-restart`: level, target 0x1F0000 and flag 1 at once, the music at volume 31); the others R]

| writer | event | target `V:4A720` | level `V:54FBE` | flag `V:4A88F` |
| --- | --- | --- | --- | --- |
| `0x1053C`, `0x10542` | start-up, before the first phrase | `0x3F0000` | `0x3F0000` | 0 (cleared at `0x10418`) |
| `0x19CE6` (the start-up phrase, 7.1) | the music starts | | | 2 |
| `0x135D0`, `0x135DA` | the any-key at the credits | `0x1F0000` | falls toward it | 1 |
| `0x11D3C`, `0x11D5A`, `0x11D70` | the main-menu build | `0x1F0000` | `0x1F0000` at once if the music is loaded (`V:4AC13` bit 7) and the flag was 0 (back from a flight) | 1 |
| `0x11E50` | the echo state after a mode key (mode above 0) | **0** | falls toward it (124 blanks from `0x1F0000` to 0, but the dispatch comes after about 48 blanks) | unchanged (1) |
| `0x1203A`, `0x12056` | the mode dispatch (the flight start) | | `0x3F0000` at once | **0**; voices 0 and 1 stopped |

The flag is the "typing sound" byte of note 08: 2 during the start-up (clicks are off, 3.6), 1 on the text screens (clicks on, ramp on), 0 in flight (no ramp: the level stays at `0x3F0000`).

### 6.3 Captures [V: `sound-menu`, `sound-menu-mode`, `sound-boot`; `sound_capture_check.py ramp`]

* The any-key at the credits (`sound-menu`): the level cell `V:54FBE` read after every blank falls from `0x3F0000` by `0x4000` per blank from the blank after the key, with the target `0x1F0000` and the flag 1
  set in the key's pass: **149 consecutive blanks agree with the rule, 0 differences**. The voice volumes of the music (both 63, then the cap) follow `max(31, 63 - ceil(k / 4))` for the blank `k` after the key
  (k = 1 is 62, k = 5 is 61) in all 140 frames checked, and reach 31 at k = 125 (the level reaches `0x1F0000` at k = 128) [V, 0 differences].
* The mode key in the menu (`sound-menu-mode`, key 5, free flight): the echo state sets the target to 0 and the level falls by `0x4000` a blank (`0x1E8000` two blanks later); **48 blanks after the key the
  dispatch cuts it**: the flag is 0 and the level `0x3F0000` at once, and in the next blank voices 0 and 1 stop (period 124, volume 0, DMA off). The fade had reached the cap 20 by then (31 at the key). So the
  150-pass echo is never silent at its end: the music is still audible, falling, when the flight start stops it.

### 6.4 The port's conversion [C, with note 08 3.8]

The ramp is a per-blank rule on a master cell, so the exact port keeps the level in the library (9.2) and moves it by `0x4000` in every library blank while the flag is set; the same level then caps the voices exactly as the original. In steps (8 a second): a whole ramp of `b` blanks lasts `(b x 8 + 25) / 50` steps (note 08 3.8, per
stage): the credits-to-menu ramp, 128 blanks, 20 steps; the fade-out of the echo, cut by the dispatch after about 48 blanks, 8 steps; one step moves the level by `0x4000 x 6.25` on average (the blank pattern of section 9).

## 7. The start-up music, the mission tone, the key click, the HUD beeps

### 7.1 The start-up sound [R; V: `sound_check.py music` and the capture `sound-boot`]

In the start-up routine `0x103CE` after the three set-up calls the level and target are set to `0x3F0000` and: with the music loaded (`V:4AC13` bit 7) `0x19CE6(0x32)`, else `0x19D7A(0x0F)`.
**`0x19CE6(v)`** with the flag `V:4A88F` = 0: with the music loaded, stop all four voices, start object `0xD` on voice 0 and object `0xE` on voice 1, both at volume `0x3F`, and set the flag to 2; without
the music but with the `text201` pair (`V:4AC12` bit 2), start objects `0x23` (voice 0) and `0x24` (voice 1) at volume `v` and set the flag to 2; with neither, stop all. With the flag not 0 it does nothing.
**`0x19D7A(v)`** starts the `text201` pair at volume `v` on voices 0 and 1 (needing `V:4AC12` bit 2), without touching the flag. The music is the object chain of 4.2: **at the first play after boot** an intro of
14 blocks of `texti0a` (2.9 s) and 2 of `texti0b` (0.4 s), then the 79.5-second loop for ever (the same on both sides); a later start replays from the counts left (7.2). It plays through the credits, the log-disk prompt and the menu. Its volume: `0x3F`
capped by the level (7.2). [V: `sound-boot`: both voices at volume 63 and period 358 from the frame 4,259 of the boot (85 s); the intro objects `texti0a` (1,022 words) and `texti0b` (1,006), then the loop; the flag `V:4A88F` reads 2 in the credits snapshot (`sound-cia`)]

### 7.2 The menu and the mode keys

| moment | what the sound does |
| --- | --- |
| the credits (flag 2) | the music plays at the cap `0x3F`; the typed text makes no click (3.6: the flag is 2) |
| any key at the credits | target `0x1F0000`, flag 1: the cap ramps from 63 to 31 over 128 blanks (6.1); clicks on |
| log-disk prompt, main menu | the typed lines click (7.4); the music goes on at the cap 31 |
| the menu build after a flight | the flag is 0: `0x19CE6(0x0F)` restarts the music at the cap 31 (the level is set to `0x1F0000` at once). The objects keep their counts (2.1): `0xD` and `0xF` (count 0 after their first play) play **one block each**, then the loop resumes from the counts it was cut at; there is no second 14-block intro [V: `sound7-restart` (milestone 7: SHIFT-ESC from the air, every register write traced): the stop of all four voices, then in the same frame objects 0xD and 0xE at **period 358, volume 31** with 0xF and 0x10 latched; 10 frames later 0x11 and 0x12 latched; the blocks that followed were 0xD x1, 0xF x1, 0x11 x2, 0x13 x1, 0x15 x3, 0x17 x1, 0x19 x3, 0x1B x1 (block lengths 1,022, 1,006, 16,385, 16,393 and 16,457 words), the counts of 2.2 plus one, the chain having been cut in the second block of 0x11's first pass; the stop of the voices and the restart came **11 blanks after the key, with no click before them** (milestone 7 correction of the first reading, which took the eleven writes of the first eleven frames for clicks: they are the sound interrupt rewriting the same period and volume into the voices 0 and 1, two writes of the same program counters in every frame, and voice 3 shows no new start; the stop and the restart are the first act of the menu set-up pass `0x11D30`, which is 33 blanks long and ends with the menu list posted at k = 44; the first click of voice 3 falls 33 blanks after the stop, the 19 instant-line starts then follow one a blank and the typed characters about every 6 to 7 blanks, note 08 5.11 [V: `sound7-restart`, `sound7-restart-at-37B18`, `-376F8`]; the port's click starts equal the capture's to the blank in 17 of 30, `sound7_check.py restart`); the level cell read 0x1F0000, the target 0x1F0000 and the flag 1 at once; the rig part `sound_check.py restart` runs the real library through cuts at 10 to 21 block ends and the cut after 15 reproduces that chain with 0 differences against the model] |
| a mode key (the echo line, 150 or 210 passes) | target 0: the cap ramps down from 31 (to 20 after the 48 blanks of the echo) and the music fades with it [V]; the dispatch then stops it |
| the mode dispatch `0x1201E` (flight start) | flag 0, voices 0 and 1 stopped, level `0x3F0000`; in free flight (mode 1) the wind starts in the next blank [V: `sound-menu-mode`: volume 8, periods 392 and 422 = `0x168` + 32 and + 62, objects 8 and 9] |
| the mission start, modes 3 to 8 (after the aircraft key of the mission flow) | `0x19D7A(0x28)`: the `text201` pair at volume 40 on voices 0 and 1 (a low hum of 34.9 Hz, beating at 0.5 %: the two voices are 179 and 181 clocks per sample), replaced by the wind at volume 8 within 40 blanks [V: `sound-flight-start-m3`: lengths 284 words, periods 179 and 181, volume 40; 40 frames later the wind at 392 and 422] |
| the flight start, the other modes | the wind at volume 8 (`0x19F9A(8)`), or the external engine pair at period `0x300` volume 8 if the wind bit is clear [V: `sound-menu-mode`, `sound-flight-start`: wind at volume 8, periods 392 and 422; the first engine pass puts the external pair on at 776 and volume 12 while the camera is outside, then the cockpit pair at 764 and volume 40] |
| the start of the approach (state `0x12C90`, modes 3 to 8; 5.7 item 10) | the wind call (volume 8) or the external pair at period `0x300`, volume 8 when the wind bit is clear; the sound routine is then gated until the approach ends |

Paths that differ from the table [R]: the **demonstration** enters the take-off at `0x12D58` directly from `0x11BD0`, with no dispatch and no approach, so the wind of the dispatch is not played; `0xFE` is posted by the player start (`0x1D564`) and the first ungated pass latches the engine objects at once. The **skip of the approach** (the KP-Enter key during it, `0x12D58` with the view at 0 and the camera switch) clears `V:4A966` at `0x12D6C` in the cockpit view: the wind of the dispatch is then replaced by the cockpit pair at the first ungated pass by the pending `0xFE`. `0x12CFE` sets the game gate and clears `V:4A966` itself (which modes enter it, and the exact entry of the KP-Enter skip, are read from the state table of note 08 6.1 and were not traced here: the rules above about the notice and the gate hold for any entry, the named paths are R and unverified). The clock tick (and with it the gear countdown, the cool-down and the head hold) runs in every pass that takes the flight path of the frame routine, i.e. whenever the game gate `V:4A84D` is set (`0x11140`/`0x11146`, the limiter at `0x11446`); the gate is set by `0x12CFE`, `0x12D00` and the other start states and cleared again by `0x12DCE` when the take-off countdown ends, so the tick runs through the approach and take-off states while the gate is set [R].

### 7.3 Pause, map, views and the crash [V: `sound-views`, `sound-crash`, `sound-eject`; the code R]

| key or event | voices stopped | what is restarted |
| --- | --- | --- |
| P (pause on) | all four (`0x115F6`), then the gate `V:4A866` blocks the per-pass routine | nothing |
| P (pause off) | none | the notice `0xFE`: the next pass restarts the engine (or the wind) from the throttle |
| M (map on) | all four (`0x115F6`); the gate `V:4A865` blocks the per-pass routine, including the cue player | nothing |
| M (map off) | voice 3 (`0x19C58(3)`) | the notice `0xFE` |
| a view change (KP-Enter, the level keys, the head keys of the external view) | voice 3 | the notice `0xFE` (`0xFF` for the direction keys of the external view) |
| the crash hand-over (cockpit view, `0x12FA0`) | all four | the crash noise `0x1A0DC(0x3F, 0x78)`; the pass routine runs again after the restart |
| ESC reset `0x11AE2` | all four | |
| the crash hand-over **in an outside view** (`V:4A83D` non-zero) | **nothing**: the stop-all and the crash noise are inside the cockpit-view test (`0x12F80`-`0x12FA0`) | the engine goes on; the notice is not changed here [R] |

The capture `sound-views` shows: KP-Enter to the outside view (the default level, `V:4A83D` = 0xFF = -1): the cockpit pair is replaced within 6 blanks by the external pair at 470 and 472, volume 36; back: the cockpit pair at 514 and 516, volume 63; **M: all four voices off (period 124, volume 0, DMA clear) for the 96 blanks of the map, the engine back 12 blanks after the key that leaves it; P: all four off for the whole pause, the engine back at the unpause**. **The pause** [V: `sound7-pause` (every audio register write traced, the program counter after each), `sound7-pause-frames`, `sound7-pause-at-376F8`, `-37B18`, `-37C2E` (milestone 7)]: the key P stops all four voices (voices 0 and 1 were the ones playing: volume 0, period 124, DMA off) within one blank of the key; the P handler
(`0x1ED9A`-`0x1EDF4`) plays nothing itself. The clicks of voice 3 seen in the pause are the typed pause text: **nine click starts, the first 13 blanks after the stop, the second 3 blanks later and then one every 2 blanks (+13, +16, +18, +20, +22, +24, +26, +28, +30; milestone 7 correction of the second value, which the first reading took as +15 from the raster positions of the writes instead of the video frames of the library blanks), each a 4-blank click (period 300, volume 2) cut by the next; the ninth one runs its 4 blanks (to +34)**
(the second capture of the same script: the same offsets, 26 frames later in the machine's clock). **A tenth start follows at +34, one pass-pair after the last character, and is not explained** (NOTE-GAP, note 08 5.14; the port makes none). **The caller is the call at `0x37B18`, not the call at `0x376F8`**: with the machine paused, a stop at the click routine `0x37C2E` was reached nine times in nine stops, a stop at
the call in `0x37B18` nine times too, and a stop at the call instruction in `0x376F8` **never in 24 windows of 2 s** (the pass counter stays fixed in the pause; the pause byte V:4A866 reads 0xFF). The attribution to the typed pause text is therefore right and the earlier address was wrong: the count is the nine click requests of the pause screen's ten-character line, of which eight characters are not blank (note 08 5.14, 3.6: the first character requests on its first two passes, the others one each, 6 passes a character), so **the characters of the pause line click through the call at `0x37B18`**; the labels of 7.4 for the two sites (`0x376F8` a typed character, `0x37B18` the end of an instant line) therefore describe different lines than the pause's. In the menu build of SHIFT-ESC both fire [V: `sound7-restart-at-37B18` and `-376F8`, no audio trace; the blank numbers of those two breakpoint runs were counted from a different origin than the key and are superseded by note 08 5.11]: 19 calls of `0x37B18`, one a blank, then 11 calls of `0x376F8`, one every 5 to 7 blanks (the menu list typed); the audible clicks are at k = 46 to 65 and from k = 70, the two earlier calls (k = 31 and 37) are not followed by a tone write. The clicks follow the characters from +13, **one every 2 blanks from the second character on, which is 6 passes of a third of a blank each, and the first character's two requests 3 blanks apart (the second pass of the screen is the long one)** (the note 08 table has the text typed from blank 16; the first click is 3 blanks earlier than that, the reading of this capture being relative to the stop of the voices, a blank after the key). The unpause: the pause byte clears and the engine pair returns at 514 and 516, volume 63 (full throttle), **4 blanks later** (`sound7-pause-frames`; the first flight pass after the byte), with no click. In the crash capture the hand-over stops voices 0, 1, 2, 3 in one frame; the engine returns 97 blanks later (the restart pass) at 764 and 40 while the crash noise is still falling (it lasts 121 blanks).

### 7.4 The key click and the HUD beeps [R for the call sites; V: `sound_check.py beeps`, 216 cases, and `sound-click`]

All are the tone of section 3.2 on voice 3 through `0x1A042`.

| routine | call | parameters p1 to p9 | duration |
| --- | --- | --- | --- |
| `0x37C2E` (click) | text engine `0x376F8` (a typed character at its first draw; V: 11 calls in the menu build of SHIFT-ESC, none in the pause) and `0x37B18` (the end of an instant line; V: 19 calls in the same menu build and the nine clicks of the pause, 7.3), when `V:4A88F` is 0 or 1 and `V:4A942` is not positive (signed) | 300, 2, 1, 300, 2, 1, 1, 1, 1 | 4 blanks, 369 Hz, volume 2 (3 %) |
| `0x37C4A` (switch beep) | the key bodies of T (`0x1DED0`), R (`0x1DEF4`), brake (`0x1E30C`), hook (`0x1E356`), RETURN (`0x1E8C2`, `0x1E8E6`), G without a gear sound (`0x1E972`), S (`0x1ED88`), J (`0x1EF34`); silent in an external view (`V:4A83D` non-zero) and while `V:4A942` > 0 | 300, v, 1, 300, v, 1, 1, 2, 2 with v = 4 (v = 2 while `V:4A88F` is not 0) | 9 blanks, 369 Hz, volume 4 |
| `0x37C32(n)` | the turn-rate warning `0x270CE` (n = 8) and gear down above `0x1200` (`0x2837E`, n = 4); silent in an external view only (**not** held back by the cool-down: it jumps past that test) | 300, 4, 1, 300, 4, 1, 1, n, n | `1 + n x (2 + n)` blanks: 81 (n = 8), 25 (n = 4); the tone does not break between the repeats (the volume stays 4 through the final wait) |
| `0x37C44` | entry used by `0x12EEC` (a beep with the flag set: v = 2, n = 2) | 300, 2, 1, 300, 2, 1, 1, 2, 2 | 9 blanks, volume 2 |

[V: `sound_check.py beeps`, the click and the three beep entries over the view, the sound flag and the cool-down, 216 cases, 0 differences.]

**T and J in the map view** [R: `0x37C4A` and the map entry, read in this round; the key bodies `0x1DED0` (T, key code `0x14`) and `0x1EF1E` (J, key code `0x26`, whose call is at `0x1EF34`)]. The only gates of the switch beep are the view byte `V:4A83D` (non-zero: nothing), then the sound flag `V:4A88F` (selects the volume 2 entry) and the cool-down `V:4A942` (positive: nothing). The key bodies of T and J make the call as their first instruction and test no map flag. The map view `V:4A865` = 1 does not appear in the beep's code, **but the map always has `V:4A83D` non-zero**: entering the map from the cockpit stores -1 in `V:4A83D` (note 06 section on the M key), and entering it from an external view leaves the external level there. So **T, J and every other switch beep are silent in the map view, through the view byte and not through the map flag** (the same holds for the clicks of the turn-rate and gear beeps, which test `V:4A83D` first). A port that mutes by the map flag gives the same result for these keys as long as the map flag implies a non-zero view byte, which it does; a port must still not mute R, G, the hook, the brake, RETURN and S by the map flag for any reason other than the view byte: they are the same routine and the same gate (all silent in the map for the same reason). The cool-down also holds for the cue tones: while `V:4A942` > 0 (the cool-down, 3 ticks of the clock, started by the cue 0x01) the cue player plays nothing and keeps its bits.

### 7.4a When the click reads the view byte [R from `0x11514`, `0x1DAA0` and the view bodies; the port's order tested in `tests/test_game_sound.c`]

The key poll `0x11514` is a loop: it asks the keyboard device for an event again and again, hands each to the handler `0x1DAA0` and returns only when the device has none, so **every key event that arrived since the last pass is handled in the one poll, in arrival order, before the pass does anything else**. A T or J therefore shares its poll with an M, KP-ENTER, `[` or `]` that came before it. The switch beep `0x37C4A` reads `V:4A83D` **live, in its own call**, when the T or J body runs. What `V:4A83D` holds at that moment depends on the earlier key of the same poll: **M and KP-ENTER do not store it** (their bodies write the request bytes: the saved-view byte `V:4A8EB`, the camera-update flags `V:4A86C` and `V:4A86D`, bit 1 of `V:4A97E`; the switch itself is made by the state routine later in the pass, `0x1426C` for the view byte), so a T after an M or KP-ENTER of the same poll still sees the byte of the pass before; `[` and `]` (the external levels, `0x1E58C`) store the level at once, but never to or from 0, so they cannot change whether the click is silent. The only change of the byte from 0 to non-zero or back is therefore the state routine and the view setters of the pass, and a click always sees the value the last pass left. The port reads it live at the event (`game.c`, `flight_button`: the weapons module's copy `ext_view` is refreshed there before T and J), not from the copy that the gun pass refreshes once per pass. The two readings differ only when a view setter runs between the gun pass and the next poll; the port keeps the live one because it is the original's.

## 8. The event table

Every trigger of the image, with the wrapper call and the numbers. "Blank 1" is the first vertical blank after the call; "stops at blank n" is the blank in which the voice is switched off (nothing audible after the previous blank).
All volumes are the start values before the master cap (section 6). The gates are on top of the per-pass routine's own (5.1): the wrappers are called from the game's routines, not from `0x14AA0`, except where
section 5 says so. [V: the wrapper arithmetic of every row is `sound_check.py noise` and `tone`; the trigger of each row is read from the code, R, unless a capture is named in the last column.]

### 8.1 Engine, wind, afterburner, gear sound end

Section 5 (the per-pass routine): voices 0 and 1; objects 0 and 1, 2 and 3 or 8 and 9; periods 470 to 837 or the wind's 360 to 423; volumes of 5.3 and 5.6.

### 8.2 Voice 2: the noise family and the other effects

[V where a capture is named in the last column: `sound_capture_check.py voice2`, 5 noise starts, 4 launches, 1 gun burst, 1 ground contact and 5 gear sounds in the logs, 1,120 of 1,145 frame samples exactly the model's period and volume, the rest within a slip of one blank (the frame
sample sees the server's write of the neighbouring blank), the stop frames equal.]

| event | trigger (instruction) and gate | wrapper and arguments | object (voice) | period | volume by blank | length | capture |
| --- | --- | --- | --- | --- | --- | --- | --- |
| a gun shot, cockpit view | the gun routine `0x17476`: `V:4A865` = 0, `V:4AC13` bit 5 set; `V:4A83D` = 0 | `0x1A2F0(6, 0x16)` | `0xA` (2) | **the object's period field, which the wrapper never sets**: 150 (the loader's value) until a burst has run to its tail, then 280 for the pulses of every later burst too; the tail sets 280 | 22, 22, 0 six times (blanks 1 to 18), then 20 falling by 0.94 a blank (20, 19, 18, ... 10 at blank 28) | stops at blank 29 | `sound-gun-launch`: volume 22, period 150, a held trigger restarts it every shot (53 frames for a 36-frame hold); `sound7-gun` (milestone 7): the first burst 150 with the tail setting 280; **the second burst, 96 blanks after the first, at 280 from its first pulse** (57 blanks, 15 pulses, the tail 20 down to 11), volume 22 [V] |
| a gun shot, outside | same, `V:4A83D` non-zero | `0x1A2F0(6, 0x1E)` | `0xA` (2) | as the cockpit row | 30, 30, 0 ... then 20 as above | stops at blank 29 | `sound7-gun`, `sound7-bang-ext` [V]: the first burst at period 150 and volume 30, the later bursts at 280 and 30 (4 bursts; 108 of 116 blanks of the four last shots equal the port's, the rest within a unit at the tail: the one-blank slip) |
| a gun shot without the gun object | same, bit 5 clear | `0x1A0DC(0x1C, 0x30)` | 6 (2) | 8,990 + 4 r | as the bump | | R (the bit is set on every machine of the captures) |
| a flare or chaff release | `0x17426` (the release request `V:4A943` non-zero, `V:4A865` = 0) | `0x1A0DC(0x1C, 0x30)` | 6 (2) | 8,990 + 4 r (15,142 and 14,002 in the captures) | 28 falling by 28/48 a blank to 0 (28, 27, 26, 26, 25, 25, 24, ... 5 at blank 40) | stops at blank 49 | `sound-gun-launch`: flare and chaff, volume 28, 49 frames |
| a pod release (mode 6, F with the shift flag) | the F key body `0x1EE0C` with the shift flag set (`V:4A930` non-zero), then `0x1EE4E`: **three tests, then the call** (rule below); the call is at `0x1EE90` | `0x1A0DC(0x1C, 0x30)` | 6 (2) | as above | as above | 49 | `sound7-pod` [V]: the mission-F4 flight snapshot (V:4A95E = 6, the latch V:4A97A = 0, the three bit-6 bytes 0x10, 0x10, 0x10), SHIFT-F: the noise at period 15,114, volume 28 falling to 0, 48 blanks, equal to the port's mission-F4 run blank for blank (48 of 48, the period too) |
| a collision, hull or landmark contact of the player at an indicated speed of `0x3C0` or less, the first contact | `0x2A834` (result `0x20`, collisions on, the hit bit not yet set) | `0x1A0DC(0x1C, 0x30)` | 6 (2) | as above | as above | 49 | R: not reached by a run-time poke (`sound7-landing`, an attempt: on the carrier deck the altitude long poked into the hull volume is put back on the deck by the step; a landmark box needs the tier-C slots of the display-list build): Open question 7 |
| the crash hand-over, cockpit view only (`V:4A83D` = 0) | `0x12FA0`, `0x12FAE` | `0x115F6` (all four voices), then `0x1A0DC(0x3F, 0x78)` | 6 (2) | 8,990 + 4 r (15,114 and 14,674 in the captures) | 63 falling by 63/120 a blank to 0 (63, 62, 61, 61, 60, 60, 59 ...) | stops at blank 121 (2.4 s) | `sound-crash`, `sound-throttle`: 120 frames, every volume equal |
| the ejection bang, **cockpit view** (`V:4A83D` = 0, which includes the camera on the seat) | `0x14E32`: the countdown `V:4A870` reaches 0 or below (5.1 item 3; the seat sets it to `0xFF`, so the same pass) | `0x1A0DC(V:4A922 >> 1, V:4A921)` = `(0x1F, 0x78)` for the seat | 6 (2) | as above | 31 falling to 0 | 121 | `sound-eject`: first volume 31, 121 frames |
| the ejection bang, **outside view** | `0x14E0E` | `0x1A0DC(V:4A922, V:4A921)` = `(0x3F, 0x78)` | 6 (2) | as above | 63 falling to 0 | 121 | `sound-eject-ext`: first volume 63, 121 frames |
| an explosion: a dying record drawn with its counter at 14 (`W0` bit `0x0400`, the record in view; the entity drawer `0x21F1C`-`0x21F62`) | the drawer sets `V:4A921` = d, `V:4A922` = d / 2 (arithmetic shift) and `V:4A870` = 4 - d / 32; d = 65,536 divided by the record's depth word shifted by `V:4AB70` and halved, a 16-bit unsigned division, **but `0x78` when the divisor is 0 or less, when the quotient exceeds `0x78` or when the division leaves a remainder** (the remainder lands in the upper word of the compared long), so d is almost always `0x78` | the bang above **in the same pass** (the entity pass precedes the sound routine; countdown 4 - 3 = 1, decremented to 0): `0x1A0DC(0x3C, 0x78)` outside, `0x1A0DC(0x1E, 0x78)` in the cockpit view | 6 (2) | as above | 60 (30 in the cockpit) falling to 0 | 121 | V (milestone 7): `sound7-bang` (the gun kill of a staged MiG 12 passes ahead, the cockpit view): three dying cycles (the wreck is counted again each time), **V:4A921 = 0x78 and V:4A922 = 0x3C each time** (d = 0x78 in 3 of 3, the remainder rule), the bang at first volume 30 (0x3C >> 1) for 121 blanks at periods 15,114, 9,030 and 12,662 (the library's draws), equal to the port's volume curve over 120 of 120 blanks; `sound7-bang-ext` (the same in the outside view): first volume 60 for 121 blanks, two bangs 32 blanks apart (the second cut the first), 120 of 120 equal; the arithmetic of d and the countdown by the rig. The other values of d (an exact division below 0x78) did not occur in the five dying cycles captured |
| the touchdown of the camera's aircraft on a surface | `0x1703A` (the descent per step above `0x180`) or `0x1704A` (otherwise); `V:4AC13` bit 2 | `0x1A18C(0x28)` or `0x1A18C(0x1E)` | 5 (2) | 310, constant | `0x28` or `0x1E` for the block | the block (15 blanks at 310) | `sound7-landing` (milestone 7): **0x1E V** (the airborne snapshot, the altitude long poked below the surface floor, the aircraft climbing: the descent per step negative; voice 2 started at period 310 and **volume 30** the frame after, DMA off 15 frames later; the port's wrapper gives the same 15 blanks at (310, 30)); **0x28 R**: five stagings of a dive did not play it (three dives at an indicated speed of 1,600 to 2,500 ended in the crash, V:4A850 = 4, with no touchdown sound; dives at 800 and 900, below 0x3C0, had the ground bit set in the first pass, at an altitude above the surface floor of the step, and also played none), see Open question 7 |
| the nose settling on the ground (first time, W2 bit 6, W0 active bit only, not on the catapult) | `0x15C44` | `0x1A18C(0x0F)` | 5 (2) | 310 | 15 | the block: DMA off 16 frames after the start | `sound-gear-touchdown`: period 310, volume 15, one block |
| the gear key | `0x1E966` sets `V:4A93D` = `0x83`; `0x14B1C` (cockpit) / `0x14B2A` (outside) in the next pass | `0x1A1E6(10)` or `0x1A1E6(5)` | `0xB` (2) | 700 falling by 7 a blank to 595 at blank 16, then 595 | 10 or 5, constant | until `0x1A24C` stops voice 2 (5.1: 1 to 2 s after the key) | `sound-cue` (the demo lowers its gear): period 700, volume 10, 61 frames |
| a missile launch by the camera's record | notice `0xFB` (`0x27668`) then the pass `0x14AA0`: `0x152C6(b, v)` | `0x1A258(0x3D)` (cockpit; outside `v6`) | `0xC` (2) | 140 + r3 (140 to 147), constant (145, 147, 146 in the captures) | 11, 13, ... 25 (blanks 1 to 8), then 61 falling by 1 a blank (61 at blank 9, 2 at blank 68) | stops at blank 69 | `sound-gun-launch` (3), `sound-cue` (1): 69 frames, every volume equal |
| a launch by another record, a hit, a kill, the cruise missile, the AI's guns | none | none | | | | **no sound of their own** (the explosion of a kill drawn in view makes the bang above) | R: the only writer of `0xFB` is `0x27668` (the camera's record); the only caller of `0x1A258` is `0x152C6`; no code on the hit or kill path calls a sound wrapper |

**The pod release's rule** [R: `0x1EE0C`-`0x1EE9A` read instruction by instruction in this round; V by nothing (no capture); the spawn side is verified in note 07 section 6]. The F key body (reached from the key code `0x23`, or from the event bit of the input word, which carries no shift flag and so never gets here) goes to the pod branch `0x1EE4E` only when the shift flag is set; without it the body is the flare release (`V:4A943` = 2, `0x17426`, the row above). The pod branch makes, in this order, **three tests and nothing else**: (1) the mission-list byte `V:4A95E` equals 6; (2) the latch word `V:4A97A` is 0 (no pod released yet); (3) **not all three of the records 1, 2 and 3 have bit 6 set in the byte at offset 1 of the record** (the bytes `V:4B43D`, `V:4B63D`, `V:4B83D`: the same slot-free test as the spawner's, note 07 section 6): one clear bit is enough. When all three hold, the branch **first ORs `0x0F` into the low nibble of `V:4B2B8`** (the ejection nibble; it is the ejection state, not the gear state) **and then, unconditionally and once, calls the noise wrapper with (`0x1C`, `0x30`)**. The wrapper has no free-slot test of its own: its guard is only the one in 8.5 (`V:4AC12` bit 0 and object 6; with the bit clear it posts the notice `0xFA` and sets `V:4A84F` = 2 instead, as 3.4 says). The value of the nibble before the OR is not read by the branch, but the key body itself is reached only through the control chain, which is skipped while the low nibble is non-zero (note 08 section 11), so the call needs a nibble of 0 in practice; the chain also needs the flow gates of note 08 (flight, `V:4A966` = 0, mode not 2). The call is **made once per qualifying key press** (the body runs once per make code; the shift flag is one-shot, note 08 section 11), in the key poll at the start of the pass, **before** the player step spawns the parachute record from the nibble (the spawner is `0x2758E`, which sets `V:4A97A`); the tests make a second press in the same pass or a later one fail (`V:4A97A` non-zero, the nibble non-zero) until the pod is gone. When test (3) fails (no free record) or the mode is not 6, **nothing sounds** and the nibble is left alone. The port therefore calls the noise (28 falling over 48 blanks) at the key, after the three tests and in the same step as the nibble is set; it is not tied to the spawn in the player step (the branch makes the call before the spawner runs and does not look at the spawner's result; the spawner's own refusals were not traced here), and not to a free sound slot (voice 2 is simply restarted, like every other noise call: a later call stops the earlier one).

**Order of the voice-2 calls inside a pass** (a later call stops the earlier) [R: the addresses in frame order]: the key poll (voice 3 beeps and the gear byte), the world update with the player's step (the nose-settle contact `0x15C44`, the touchdown contacts `0x1703A`/`0x1704A`, the collision bump `0x2A834`, in that order), the entity pass (sets the bang bytes), the gun routine (`0x1749A`/`0x174AC` or the noise, the flare and chaff `0x17426`), then the sound routine (the gear start or stop, then the bang, then the engine block on voices 0 and 1). The crash hand-over `0x12FA0` is in the dispatcher state and stops all voices before its noise. So a bump in the same pass as a gun shot is cut by the shot, and a bang cuts both. (`r` is the 11-bit draw of 3.4, `r3` the 3-bit draw.) The same voice cannot play two of these at once: the later call stops the earlier (a gun burst cuts a thud, a bump cuts the gear sound); the engine, wind and music are on voices 0 and 1 and are not affected.
**Corrections to note 07 13.4 (made there, with the other rows in this note):** its table assigned the ejection bang's two call sites the other way round (it reads `0x14E0E` as the cockpit view and `0x14E32` as the halved one); the code tests the cockpit view (`V:4A83D` = 0) and takes `0x14E32`, the halved amplitude, for it, and the captures agree.

### 8.3 Voice 3: the cue tones, the seat tone, the beeps and clicks [V: the arithmetic `sound_check.py tone` (312 sets) and `beeps` (216 cases); in the captures `sound_capture_check.py tone`, 53 tones, 0 differences: the cues 0x02 (70 frames), 0x80 (twice), 0x20, the in-box pulses 0x100, the alert 0x04 until stopped, the seat tone, the click and the beeps 2, 4; milestone 7, `sound7-cues`: the cues 0x10 (36 blanks), 0x800 (24), 0x40 (173) and 0x01 (130), each poked into the cue long and consumed in the next pass, **394 of 394 blanks equal to the port's** (periods and volumes); `sound7-cues-hold`: the cue 0x10 poked 20 frames after 0x01 started only 107 and 136 blanks after it (twice, at two phases of the second): the cool-down of three ticks, 100 to 150 blanks (the port's 132 is inside), as 5.1 item 2's tick predicts]

The cue bits of `V:4AC0C` and their nine parameters are the table of note 07 13.4; the arithmetic (3.2) gives:

| cue | parameters | first period, volume | second | length | pitch (first, second) |
| --- | --- | --- | --- | --- | --- |
| `0x40` the eject clause | 160, 16, 8, 200, 16, 5, 3, 4, 4 | 160, 16 | 200, 16 | 173 blanks | 693 Hz, 554 Hz |
| `0x80` crash imminent, proximity | 324, 10, 8, 200, 10, 5, 3, 1, 4 | 324, 10 | 200, 10 | 44 | 342 Hz, 554 Hz |
| `0x01` a hostile missile locked or fired | 124, 16, 8, 180, 16, 5, 3, 3, 4 | 124, 16 | 180, 16 | 130 | 894 Hz, 616 Hz |
| `0x04` a new alert message with the sound byte | 229, 3, 5, 229, 3, 5, 1, 0, 1 | 229, 3 | 229, 3 | for ever until voice 3 is stopped | 484 Hz |
| `0x100` the target enters the lock box | 192, 4, 9, 200, 0, 3, 1, 0, 1 | 192, 4 | 200, 0 | for ever (pulses of 9 blanks on, 4 off) | 577 Hz |
| `0x10` the missile lock | 500, 15, 5, 500, 15, 5, 3, 1, 5 | 500, 15 | 500, 15 | 36 | 222 Hz |
| `0x02` threat bit 3 | 500, 10, 5, 130, 0, 1, 3, 3, 5 | 500, 10 | 130, 0 | 70 (three pulses of 5 blanks, 1 off) | 222 Hz |
| `0x20` the stall lamp | 400, 8, 5, 330, 0, 1, 3, 2, 5 | 400, 8 | 330, 0 | 47 | 277 Hz |
| `0x800` threat bit 1 | 500, 10, 5, 130, 0, 1, 3, 1, 5 | 500, 10 | 130, 0 | 24 | 222 Hz |
| `0x08` | stops voice 3 | | | | |
| the seat tone `0x26FFE`, called by the seat routine `0x26F8E` when the ejection nibble is 9 **and `V:4A83D` = 0** (the cockpit view, including the camera on the seat; note 07 says the external view: corrected there) | 200, 14, 8, 200, 0, 2, 1, 5, 1 | 200, 14 | 200, 0 | 56 | 554 Hz |
| click, switch beep, turn-rate and gear beeps | 7.4 | 300, 2 or 4 | | 4, 9, 81, 25 | 369 Hz |

Pitch is the square wave's 3,546,895 / 32 / period. Only the cue player (`0x14B5A`) plays the cue bits, one per pass, in the order of note 07; a cue plays nothing while `V:4A942` > 0.

### 8.4 Voices 0 and 1: the start-up music, the text201 pair, the wind and the engine

Section 7 for the music and the mission tone, section 5 for the rest.

### 8.5 The guards of the wrappers in one table

| wrapper | needs |
| --- | --- |
| `0x19DB2` | `V:4AC13` bit 0 and object 1 (bit clear: it calls `0x19E46` with volume / 4) |
| `0x19E46` | `V:4AC13` bit 1 and object 3 |
| `0x19EFA` (and `0x19EBE`) | `V:4AC13` bit 1 and a non-empty voice 1 (`0x19EBE` divides the volume by 4 when bit 0 is clear) |
| `0x19F9A` | `V:4AC13` bit 4 and object 9 (bit clear: it stops voices 0 and 1) |
| `0x1A042` | object 4 |
| `0x1A0DC` | `V:4AC12` bit 0 and object 6 (bit clear: the notice `0xFA` and `V:4A84F` = 2 instead) |
| `0x1A2F0` | object `0xA` |
| `0x1A18C`, `0x1A1E6` | `V:4AC13` bit 2 or 6, the object, and the stepped record being the camera's (`V:4AA6E` = `V:4A996`) |
| `0x1A258` | `V:4AC12` bit 1 and object `0xC` |
| `0x19CE6`, `0x19D7A` | 7.1 |

## 9. The pass-to-blank timing and the port's cut line

### 9.1 What runs when [R for the rates; V for the phases: a start's registers in the pass and the script in the next blank (`sound-click`: the load in the pass, the first period and volume in the next blank, the stop in the fourth; the rig's register log, every part)]

| what | rate | phase |
| --- | --- | --- |
| the per-blank server `0x551D8` (scripts, registers, steps, timers) and the ramp `0x294AC` | every blank (50 Hz PAL) | the vertical-blank interrupt; the server runs after the game's own server |
| the per-pass routine `0x14AA0` and every wrapper | once per pass (about 8 a second in flight) | in the frame routine, after the alert arbiter; the interrupt handler runs at once inside the wrapper |
| the engine retarget | every 4th pass, or at once after a notice | `V:4A879` low bits 0 |
| the clock tick (the gear countdown, the cool-down, the head hold) | once per change of the system-clock seconds: **50 blanks** (49.97 frames a second over 5,896 frames) | in the first drawn pass that sees the new seconds long; V (`sound7-tick`): 13 of 13 decrements in the sample of the change; the phase against the mission is the machine's clock, arbitrary |

A wrapper's start writes the object's period and volume registers **at once** (inside the call, by the handler); a script starts at the **next** blank (wait 1). A glide (`0x19EFA`) begins at
the next blank: the registers keep the old values until then, because the steps are added after the write (4.3). The sound therefore changes in the blank after the pass at the latest.

### 9.2 The cut line and the contract between the core and the mixer [C: the division of work chosen for Tasks 5 and 6; the library rules it relies on are V]

The plan's `game_audio_cmd_t` (START, SET, STOP with object, voice, period, volume and byte ranges) and its master target "0 to 63 in `0x4000` units" cannot express scripts, glides, the block chain, persistent objects (2.1) or the three master inputs below, and the unit is wrong (the target is in volume units: `0x1F0000` is 31, the level moves a quarter unit per blank). The single `scene.h` change of Task 5 replaces them with the following contract.

**The sound library is a pure integer module of the core** (`sound.c`; no floating point, no OS calls; one session-long state):
the 37 object records of 2.1 (fields as the original's, 16.16 where it is), the four voice slots, the four voices' hardware registers (location id, length, period, volume, DMA on), the three master cells, the random register of 3.3 and, per voice, the colour clocks left in the current block. It is stepped by the simulation, not by the audio device, so the goldens stay integer: **each simulation step runs 6, 6, 6 or 7 library blanks (25 per 4 steps; step `k` covers blanks `floor(25k/4)` to `floor(25(k+1)/4) - 1`)**, and each blank is the original's order: **first the master ramp of section 6** (called by the game's own vertical-blank server, node priority 0) **and then the sound server of 4.3** (scripts, register writes, steps, timers; node priority -120, so it runs after) [R for the priorities; V in `sound-menu`: the voice volume of blank 1 after the key already reads the lowered cap 62, i.e. the ramp step precedes the write of that blank].

| input of the library | arguments | mirrors |
| --- | --- | --- |
| wrapper calls, in the order a pass makes them, applied **at the pass, before the blank loop of that step** (so a start's registers exist from the first blank, as the handler's writes do); the one exception is the typed text's key click, whose pass runs the step's blanks up to the blank of the request first (`sound_run_to`, 5.7 item 13; the text screens call no other wrapper, a flight step no click) | the calls of 5.4 and 8: `engine_cockpit(pitch, volume)`, `engine_ext(pitch, volume)`, `glide(pitch, volume, ticks)`, `launch(volume)`, `wind(volume)`, `gear_start(volume)`, `gear_stop()`, `tone(p1..p9)`, `noise(amplitude, duration)`, `gun(pulses, volume)`, `ground(volume)`, `phrase(volume)`, `mission_tone(volume)`, `stop(voice)`, `stop_all()` | `0x19DB2`, `0x19E46`, `0x19EFA`/`0x19EBE`, `0x1A258`, `0x19F9A`, `0x1A1E6`, `0x1A24C`, `0x1A042`, `0x1A0DC`, `0x1A2F0`, `0x1A18C`, `0x19CE6`, `0x19D7A`, `0x19C58`, `0x115F6` |
| master inputs from the game flow (the library keeps the level; the scene carries the target, the immediate level and the flag only as information) | `set_target(units16_16)` (the game's `V:4A720`), `set_level_now(units16_16)` (a store to `V:54FBE`), `set_flag(0, 1 or 2)` (`V:4A88F`) | 6.2 |
| block ends | none: the library keeps the colour clocks left of each voice's current block (see the rule below) and runs the handler of 4.2 itself when it reaches 0 | the audio interrupt |

The random draws (noise 11 bits, wind 6 + 6, launch 3, and the 16,384 bits of the noise buffer at start-up) come from the library's own register (3.3). The core's per-pass routine (section 5), the cue player and the trigger points (section 8) stay in the game modules and call the library.

**The block clock, exactly as the hardware keeps it.** A sample lasts the period register's value *at the moment the sample starts*: the period counter is reloaded from the latch at every sample end. A block of `n` bytes ends after `n` samples, so its duration is the sum of the periods in force sample by sample, not `n` times the period at the block's start. A glide (or a script step) inside a block therefore changes when it ends. The library keeps per voice the samples left in the block and a clock fraction, and in each blank consumes `71,051 / period` samples (fractional, carried; the period is the register's value for that blank, the pass's writes before the blank loop and the server's writes at the blank). A block end inside the blank runs the handler of 4.2 at that point of the blank's order (after the server of that blank). Only looping engine sounds glide, and their blocks loop to themselves, so the exact end matters in practice for the music (constant 358) and the ground contact (constant 310), where the two rules agree [R; V: the block schedule of 4.2 for the constant case].

**When a block end's writes take effect inside the blank.** The handler runs at the interrupt, which the hardware raises when the block's last sample has been fetched, i.e. at the colour-clock position where the block ends. The latch, DMA, period and volume writes of that run therefore take effect **at that sample position**, not at the blank's start, and the sample playback switches to the newly latched block at that same sample. The stream the library emits carries, for each write made by a block-end handler, the colour clock within the blank at which the block ended (the library knows it from its sample counter); writes made by a wrapper (at the pass) carry clock 0 of their blank and the server's carry clock 0 as well. The mixer applies each write at the output sample nearest to its clock [R for the hardware; C for the stamp].

**Live-play rules of the mixer** [C: port rules, no original counterpart; the frontend implements them in `audio.c`, `audio_apply`, and they are what the tests of `tests/frontend/test_audio.py` pin]. The rules give up nothing of the contract above: **every register write reaches the voices, once, in the order the library made it, at its stamped position relative to the other writes and to the voices' own block ends**; only the position of the whole stream against the device's clock is chosen, and the only audible deviations are a clock rate within 0.5 % and, in the two rare cases below, a short gap or a skipped stretch of output. The mixer's time is the game's time (blank x 71,051 + clock, in colour clocks) plus one offset; the rules move that offset or the clock rate and never a single write against its neighbours.
(a) **[C] The anchor and the lead.** The first step is placed 3 blanks (about 60 ms) ahead of the output time: the offset is the output time plus 3 blanks minus the start of step 0. Later steps keep the offset, each at its wall-clock start `k x 6.25` blanks (the writes' own blanks, `floor(25 k / 4)`, lie 0 to 0.75 blank earlier, which the 3-blank lead absorbs). The lead measured at a step's start is smoothed (an eighth of the difference per step); the callbacks and the frames make it jitter by about +-1 blank around 3.
(b) **[C] The drift trim.** The two clocks (the wall clock that times the steps, the device's sample clock) differ by up to a few tenths of a percent; the mixer follows the smoothed lead with a clock trim that would close the difference in about 32 steps (4 s), **at most 0.5 %** (about 9 cents of pitch, and the same rate change of the timing, inaudible). Because the block durations are counted in the mixer's colour-clock units and not in output samples, the trim rescales the whole stream, writes and block ends alike, and keeps their relative positions exactly.
(c) **[C] The underrun** (a step arrives after the output has passed the end of the stream received so far, the end of the last blank of the last step applied): the mixer does not advance the voices past that end: the voices stand still, their registers hold, and the output holds its last sample (no click) while it waits; the underrun is counted and reported once, and the offset is **re-anchored**: the voices, the writes waiting and the offset move together so that the new step continues from the held position, placed 3 blanks ahead of the output again, the trim is reset. The late blanks are not applied "at once" (as an earlier draft had it): that would collapse the blanks' relative positions, and a block end's writes would take effect before the mixer's own end of the block, as in (d). Every write is applied, in order, at its new position; what is audible is a hold of the output sample for the length of the lateness, the output time being neither stretched nor skipped. A chained voice (the music, the ground contact) whose block end would fall in the gap therefore does not repeat its old latch: its next block starts at the position of the stream, so the chain is not late; looping single-block voices (the engine, the wind) are held as well. Implemented in `audio.c` (`send`, the end of the stream, bounds `run_until` in `audio_render`; pinned by `test_a_chain_is_not_late_after_a_stall_whose_gap_holds_block_ends`: the hold equals the pause to the callback's 64 frames, where the previous mixer's was a whole number of blocks, up to 993 frames off).
(d) **[C] The surplus** (a step begins more than **5** blanks ahead of the output: a burst after a stall, or a long drift): the mixer **skips whole blanks of output**: it runs its event model (every write, every block end, in order, at its own position) up to the target and does not render those blanks, so that the lead returns to 3. It never applies a block end's latch early, which "the two oldest blanks applied as one" would: the latch and the period and volume writes of a block end would then take effect a blank before the mixer's own end of the block and re-create the block-end timing error of the previous section permanently (the voice's block would be out of phase with the library's from then on). The threshold is 5 and not 4 because the lead jitters by +-1 around 3, and a threshold at 4 would skip in normal play; the skip is one stretch of silence or of sustained sound of at most a few blanks, counted in the statistics. The trim is reset.
(e) **[C] The clock rate.** The core makes 50 blanks a second (8 steps of 6.25) and so do the port's clocks: the `--wav` mixer and the device mixer count 3,552,550 colour clocks a second (50 x 71,051), 0.16 % more than the machine's 3,546,895 (49.92 blanks a second, 71,051 per 20.03 ms). Every sound is therefore 0.16 % faster in time and in pitch than the original's, in step with the game (whose clock is the same 50 a second); the machine's constant is used only where the period register is turned into a sample length (1.4) and in the rig comparisons. The step rate is not the cause of drift: with the port's own clock on both sides, the trim of (b) corrects only the difference between the wall clock and the device.
(f) **[C] `--wav`** renders no live rule: each step is applied and then rendered for exactly its own 6 or 7 blanks (882 frames a blank at 44,100 Hz, 125 ms a step on average, so the file is `floor(25 k / 4) x 882` frames after `k` steps), after the writes of the step have been queued, so the file depends neither on a device nor on the wall clock; this is the grid the tests compare frame for frame.
(g) **[C]** A core stopped for a pause or a menu stops emitting steps; the mixer holds the last registers (the original stops all voices at a pause, so this only covers frames without a library step); on the first step after the stop rule (c) re-anchors.
(h) **[C] The output gain.** The mixer's per-voice sample is the signed 8-bit sample times the 6-bit volume (0 to 64) **times 2**: full scale 128 x 64 x 2 = 16,384 a voice, so that two voices a side (the stereo hard pan puts two of the four on each side) never clip in a 16-bit output. The factor 2 is a port choice with no counterpart in the original (the chip's own mix has no digital gain; the analogue stage is not modelled); the A500 filters of 1.5 are applied after it. It changes loudness only, never a register value, and the goldens do not see it.

The text-screen click placement (the formula the port must use): the text engine runs against a budget `B` units per step (37,500 by default; the engine scales it for a list with a stretched typing rate, note 08 3.8 rule 3) with a pass costing `1,000 + 1,500 x clicks + 285 x glyph blits` units (the mission texts: 5,100 per click). If a step runs `nb` library blanks (6 or 7) and the passes of that step have cost `c` units in all up to and including the pass that makes the request, the request is made at library blank `floor(c x nb / B)` of that step (clamped to `nb - 1`), counted from the step's first blank. A click needs 4 blanks to finish, so requests in the same or the next three blanks cut it.

**The mixer implements Paula only.** Each simulation step the library emits the register writes it made, in order (`game_audio_cmd_t`: `op`, `voice`, `object`, `blank`, `value`, `offset`, `length`, `clock`), each stamped with the blank of the step (0 to 6) and the colour clock within it: `LATCH(object, offset, length)` (AUDxLC and AUDxLEN: the byte range of the object's buffer; the file's range or the core's tone and noise buffers), `DMA_ON` and `DMA_OFF`, `PERIOD(value)`, `VOLUME(value)`. Only the writes that change a register are issued. The mixer plays them as the hardware does (1.4): at DMA on it starts the latched block, at a block's end it restarts the latched block (the hardware repeat; a LATCH written meanwhile takes effect at that end), the period and volume registers apply at once. The queue between the two is ordered by time (stable): the core runs ahead of the audio device by whatever the queue holds, the mixer applies a write when its output time reaches `(blank + clock/71,051)` blanks of the game's time plus the offset of the live rules (a blank is 71,051 colour clocks, 20.03 ms on the machine, 20 ms in the port, (e)). At one instant the order is: the samples that end first (a block end restarts the block latched before the writes of that instant), then the writes in the order given, then the period reads of the samples that begin. Sample data are the object's byte range (2.2) by location id.
The filter of 1.5 is applied by the mixer. The master level is entirely inside the library (it caps the volume written), so the mixer has no master input.

**Why not a blank clock in the mixer with the core posting calls**: the block-chain advance, the random draws and the persistent objects would then depend on the audio device's timing, and the goldens (the command log per tick) could not be integer-stable. The registers of the original are the natural boundary: the audio command log is the register-write list per blank, and the rig's custom-register write log is its witness (`sound_check.py` compares exactly these lists, per blank, with the model).

### 9.3 Conversion to steps (note 08 3.8)

The simulation step is 8 a second, so 25 blanks pass in 4 steps (50 blanks a second; the machine's 49.92 makes the library 0.16 % fast, 9.2). Step `k` covers blanks `floor(25 k / 4)` to `floor(25 (k + 1) / 4) - 1`, a pattern of 6, 6, 6, 7 blanks. Durations of whole sounds in steps by the 3.8 rule `(blanks x 8 + 25) / 50`:
click 4 blanks = 1 step; the turn-rate warning 81 blanks = 13; the gun 29 blanks = 5; the bump 49 = 8; the crash 121 = 20; the launch 69 = 11; the master ramp 128 blanks = 20 steps.
These are for displays and tests.

## Constants

| constant | value | address or source |
| --- | --- | --- |
| objects | 37 (0 to `0x24`), object 7 unused | `0x5591C` |
| object record | `0x40` bytes | `0x55504` |
| default period, volume, count | `0x166` (358), `0x3F`, 1 | `0x55504` |
| minimum period written | `0x7C` (124) | `0x55260`, `0x55158` |
| volume mask | `0x3F`; limited by the hi word of `V:54FBE` | `0x55260` |
| PAL sample clock | 3,546,895 Hz / period | hardware |
| voice slots | `0x54ED0` (4 longs) | `0x54ED0` |
| channel descriptors | `0x54EE0`, `0x1C` bytes each; table `0x54EC0` | `0x54EC0` |
| the audio interrupt handler, the blank server | `0x55158`, `0x551D8` (node priority -120) | `0x54F50`, `0x54FA8` |
| the random register and seed | `0x55C9C`, `0x47091247`; taps at bits 0 and 3 | `0x55BE4` |
| noise buffer, tone buffer | 2,048 and 32 bytes | `0x19AAA`, `0x19A6C` |
| noise period | `0x231E + 4 r`, r 11 bits | `0x1A0DC` |
| wind period | `0x168 + r`, r 6 bits, per voice | `0x19F9A` |
| launch period | `0x8C + r`, r 3 bits | `0x1A258` |
| engine pitch, cockpit / outside | `0x310 - 2a - a/4`, `0x320 - 2.75 a` | `0x14F70`, `0x15096` |
| voice 1 detune | +2 | `0x19DB2`, `0x19E46` |
| glide length | 32 blanks (`0x20`) | `0x15072`, `0x151CA`, `0x15220` |
| engine gate | every 4th pass | `0x14E40` |
| engine volume (idle) | `0x0C` | `0x154D6` |
| wind volume (seat) | `0x10` | `0x1551A` |
| afterburner-hold counts | 6 (on), 8 (launch), 2 (overspeed, noise failure) | `V:4A84F` |
| gear sound volumes | 10 cockpit, 5 outside | `0x14B26`, `0x14B18` |
| gun | 6 pulses, volumes `0x16` / `0x1E`, period 150 then 280 for good after the first tail | `0x1749A`, `0x174AC`, `0x55D98` |
| crash, ejection bang | `(0x3F, 0x78)` | `0x12FAE`, `0x14E0E` |
| bump, release | `(0x1C, 0x30)` | `0x2A834`, `0x17426`, `0x1EE90` |
| ground contact volumes | `0x0F`, `0x1E`, `0x28`; period `0x136` | `0x15C44`, `0x1704A`, `0x1703A` |
| master ramp step | `0x4000` per blank; range 0 to `0x3F0000`; menu target `0x1F0000` | `0x294AC`, `0x135D0` |
| music loaded if free chip memory | above 200,000 bytes | `0x10476` |

## Open questions

1. **Settled by this note** (the items carried by notes 05 to 08 and the plan): (a) *Which wrapper a non-camera launch uses*: none. The only writer of the notice `0xFB` is `0x27668`, under the test that the launching record is the camera's, and the only caller of the launch wrapper
   `0x1A258` is `0x152C6`, reached from that notice; a launch by another record, a hostile or the AI makes no sound [R; V: `sound-gun-launch`, `sound-cue`: every voice-2 launch in the logs has the camera's `0xFB` before it; the demo's enemies fire and nothing plays]. (b) *The 150-pass echo and `V:4AC13` bit 7*: the bit says the music
   is loaded; the echo of the mode key runs with the music audible and falling (target 0), and the dispatch stops it about 48 blanks after the key (6.3, 7.2). (c) *`gun_sound` (`V:4AC13` bit 5)*: set whenever the noise buffer and its clone were created; the enable longs read `0x000007F7` on the reference machine
   (`sound-cia`), so it is 1 with the shipped disk; it is 0 only when the 2,048-byte chip allocation failed. (d) *The filter*: the game never writes the CIA, the LED bit is 0 (filter on) on the reference machine and the fixed RC stage is always present: the default is on, with the corners of 1.5 [V for the bit, C for the choice]. (e) *The stereo pairing*: the hardware's, verified for voices 2 and 3. (f) *The playback period of each file* (2.4).
2. **The pitch and volume parameters of the cues and the beeps** (carried by note 05 17.13 and note 07 13.4): the tables of 3.2, 7.4 and 8.3, with durations, are complete for the nine cue bits, the seat tone, the click and the three beeps.
3. **The sound on a machine with less free chip memory**: the loader skips the music when `AvailMem` (chip) is not above 200,000 bytes, and any later allocation can fail; the objects and enable bits then differ (the start-up sound is the `text201` pair, the wrappers whose bit is clear do nothing or post `0xFA`). The reference machine
   has the music. The port applies the loader's per-file rules (a missing file or object leaves its enable bit clear and its sounds out; plan Review Focus 2); the one choice is the memory test, taken as true (free chip memory above 200,000), as on the reference machine; the missing-object paths are in the model and the rig (the pass part clears each enable bit in turn, 0 differences).
   **Closed as unobservable (milestone 7):** the test reads the free chip memory of the machine at the moment of the load, a property of the host configuration and not of the game; every capture ran the A500 with 512 KB of chip memory and loaded the music (the enable long read 0x000007F7 in `sound-cia`, the chain played in `sound-boot`, `sound7-restart`), and the rig's post-flight restart (`sound_check.py restart`, 10 cuts, 0 differences) shows no path in the game that clears the music's enable bit or the objects' counts after the load. No script can pick another memory size; the port keeps the test as true.
4. **The phase of a pass inside a blank**: the frame samples cannot show it (a script started by a pass runs in the next blank's server; the handler's own writes are in the pass). The port quantises to blanks (9.1).
   **Answered by the per-write traces (milestone 7, V):** the traces of `sound7-pause`, `sound7-restart` and `sound-click` carry the raster line and colour clock of every register write: a start's writes fall at the line of the pass that made them (lines 12 to 300 of the frame, any: e.g. the nine pause clicks at lines 265, 113, 88, 30, 32, 32, 32, 32, 33 of successive frames, the pass drifting by the pass length), the first period and volume at the next server (line 12 to 14 or the last lines, the server runs after the game's own vertical-blank server). So a pass has a free phase inside the blank and the port's quantisation to blanks (a start at blank 0, its script at the next) loses at most one blank, the slip the frame samples already show (1,120 of 1,145 exact, the rest within one blank). The pass-to-blank cost model that places a pass is the port's (C): the measured pass lengths are a third of a blank in the pause screen (2 blanks a character at 6 passes), about 1 blank or less on the menu build's typing, 6 blanks in flight and about 13 in the approach (`sound7-pause`, `-restart`, `-tick`, `-approach`).
5. **The interrupt priority and the server order** (the blank server after the game's own, priority -120) are R; the observed writes of the server fall between lines 13 and 90 of the frame, depending on what the CPU is doing. No rule depends on it.
6. **Unused code and data**: object 7; `textegn2` (never loaded while `textegn` exists); the shut-down `0x553D8` (called only after a failed start, `0x10412`) and the free-all routine `0x558F6` (no caller); the waveform builder `0x559C0` (a three-segment generator on the floating-point library, no caller: R); the notice `0xFF` reaches only the default row.
7. **Captured in milestone 7** (each with its section): the explosion bang and its distance quirk (8.2: `sound7-bang`, `sound7-bang-ext`: d = 0x78 in all five dying cycles, first volume 30 in the cockpit view and 60 outside, the port's curve equal over 120 of 120 blanks), two gun bursts more than 28 blanks apart (the second burst at 280 from its first pulse) and the gun shot outside (volume 30) (8.2: `sound7-gun`), the music restart after a flight (7.2: `sound7-restart` and the rig part `sound_check.py restart`), the clicks of the pause and the P beep (7.3: `sound7-pause`, `-pause-frames`, `-pause-at-*`; the caller is the call at `0x37B18`, nine clicks), the overwrite of a pending bang (5.1 item 3: `sound7-bang-overwrite`), the cue bits `0x40`, `0x01`, `0x10` and `0x800` (8.3: `sound7-cues`, `sound7-cues-hold`: 394 of 394 blanks equal), the gear sound's outside volume with V:4A86D clear (5.1 item 2: none, `sound7-fixedcam`), the gear sound with the camera on the seat (5.1 item 2 and 5.7 item 12: `sound7-seat`), the pod release (8.2: `sound7-pod`, 48 of 48 blanks), the ground contact volume `0x1E` (8.2: `sound7-landing`), the swallow (5.1 item 7: `sound7-swallow`), the seconds tick (5.1 item 2 and 9.1: `sound7-tick`) and the sound routine in an approach (5.7 item 10: `sound7-approach`).
   **Not captured, and why:** (a) the ground-contact touchdown volume `0x28` and (b) the bump `0x2A834` of a first hull or landmark contact: both are in the player's step, whose inputs (the descent per step is the record's smoothed velocity, not a poked word; the contact result is a hull or landmark volume of the tier-C build) a run-time poke cannot set without the aircraft being moved back by the same step; the stagings tried are in 8.2. Both are rig-verified arithmetic (the wrapper calls with the volume and the guards, `sound_check.py noise`, 400 runs) and the port plays them from the same tables; a capture needs a real landing on a runway at the right descent rate, or a real taxi into a hangar, which no key script of this milestone flies. (c) The other values of d of the bang (an exact division below 0x78): none occurred in five dying cycles; the arithmetic is the rig's. (d) The music-less machine (Open question 3): the host's memory.

## Verified by

**Rig** (`tools/m68k-rig`, `make -C tools/m68k-rig test` green; the sound library runs on the real image and the real sample files with the operating system's calls replaced by hooks, a custom-register write log, and the audio interrupt delivered by the rig: `rig_io_log_set`, `rig_io_read_zero`, `rig_irq_audio`, `rig_hook_result`):

| script, part | what it checks | count |
| --- | --- | ---: |
| `tests/lists_model/sound_check.py objects` | all 37 object pointers, every field of every record, the file bytes under each data range, the square wave, the noise buffer, the links, the scripts' fields, the enable bits | 157 checks, 0 differences |
| `sound_check.py tone` | `0x1A042` over the ten cue sets, the click and beep sets and 300 random sets: every register write of every blank until the voice stops | 312 sets, 0 differences |
| `sound_check.py noise` | the noise buffer against the register sequence from the seed; the noise, gun, ground, gear, launch and wind wrappers over random arguments, with random stops in between: every register write | 400 runs, 0 differences |
| `sound_check.py music` | the phrase wrappers `0x19CE6` and `0x19D7A` in seven enable/flag variants; 120 block ends per voice of the object chain | 0 differences |
| `sound_check.py beeps` | the click and the three beep entries over view, flag and cool-down: the nine tone parameters | 216 cases, 0 differences |
| `sound_check.py pass` | `0x14AA0` and the library over random states (gates, notices, views, records of class `0x3x`, enable bits cleared one at a time), six blanks after each pass, every register write | 2,000 passes (12,000 blanks); 30,000 passes in a longer run, 0 differences |
| `sound_check.py ramp` | `0x294AC` over random flag, target and level | 2,000 calls, 0 differences |
| `cue_check.py` (note 07) | the cue player's order and parameters | 310 runs, 0 differences |

The model of these rules is `tests/lists_model/sndmodel.py` (the library and the per-pass arithmetic), the harness `sndlib.py`.

**Reference emulator** (`audtrace on frame` logs one line per frame of the voices' registers, `wav` records the stereo output; scripts generated by `tools/vamiga-ref/scripts/gen-sound.py`; the checks are `tests/lists_model/sound_capture_check.py`, which reads the git-ignored logs):

| capture | what it shows | check, count |
| --- | --- | --- |
| `sound-click` (per-write trace) | the click: DMA on in the pass, period 300 volume 2 for 3 blanks, off in the 4th; the register order of a start (location, length, DMA, period, volume) | `click`: 5 uninterrupted clicks, 4 blanks each |
| `sound-menu` | the any-key ramp, the log-disk prompt and the menu | `ramp`: 149 blanks of the level cell, 140 voice frames, 0 differences |
| `sound-boot` | the cold boot, 8,200 frames: the music from frame 4,259 | `chain`: 12 changes within 0.7 frame of the block schedule |
| `sound-cia` | CIA ports, the enable longs, level and flag in the credits snapshot | 1.5, 2.3 |
| `sound-menu-mode` | key 5 in the menu: the fade, the dispatch, the wind, the first engine | 6.3, 7.2 |
| `sound-throttle`, `-afterburner`, `-views`, `-gun-launch`, `-gear`, `-gear-touchdown`, `-eject`, `-eject-ext`, `-crash`, `-cue` | engine, wind, afterburner, views, map, pause, weapons, gear, bump, ejection, crash, the demo's cues | `engine`: 810 of 811 settled samples equal the model's target; `tone`: 53 tones, 0 differences; `voice2`: 16 effects, 0 differences |
| `sound-flight-start`, `-m3` | free flight and the mission start | 7.2 |
| `sound-crash` recording | stereo pairing | `stereo`: 0 differences |
| `sound7-gun`, `-bang`, `-bang-ext`, `-bang-overwrite` (milestone 7) | the gun bursts (150 then 280, volume 22 and 30), the explosion bang (volume 30 and 60, 121 blanks), the overwritten bang | `tools/vamiga-ref/sound7_check.py`: bang 120 of 120 blanks, bang-ext 120 of 120, overwrite 80 of 80 and 96 of 96, gun 108 of 116 (the rest the one-blank slip) |
| `sound7-cues`, `-cues-hold`, `-pod` | the cues 0x10, 0x800, 0x40, 0x01; the cool-down of three ticks; the pod release | cues 394 of 394 blanks; the hold 107 and 136 blanks (100 to 150); pod 48 of 48 |
| `sound7-fixedcam`, `-seat`, `-swallow`, `-tick`, `-landing` | the gear sound by camera, the order of the sound routine and the tick, the seconds tick over 120 s, the ground contact | the fixed camera plays none, the seat plays at 10 (the port does not: a deviation); the tick: 13 of 13 decrements in the sample of the change; the touchdown volume 30 |
| `sound7-approach`, `-restart`, `-pause*` | the routine's entry in 210 passes of a mission start; the post-flight menu build; the pause clicks and their caller | 125 gated entries, 2 + 6 ungated; the chain texti0a x1, texti0b x1, texti1 x2, ... equal to the rig's restart after 15 block ends; nine clicks at +13, +16, +18, ..., +30 blanks from `0x37B18` |
| `tools/m68k-rig/tests/lists_model/sound_check.py restart` (rig) | the menu build's restart at ten cut points of the object chain, write by write against the model | 10 cuts, 0 differences |

The demonstration plays the flight sound like a flight (`sound-cue` is the demo). Cue bits set during the map or the pause wait for the cue player, which is gated there; they are played one at a time afterwards and the long is cleared after each (note 07 13.4).

Everything of this note not named above is read from the code and marked R.

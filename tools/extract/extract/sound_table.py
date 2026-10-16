"""tables/sounds.bin (extractor version 25): the 37 sound objects of note 10 section 2.2, as the game's loaders build them.

The objects are not stored in the image: the loaders (note 10 2.3) read the files of text/ and build the records at run time. This module applies the loaders'
rules to the lengths of the files on the disk, so the table carries the byte range each object plays. Records are 20 bytes, big-endian, object number n at 20 n:

    +0  u8  file    index into sounds/index.txt; 0xFF for a synthesised object and for an absent one
    +1  u8  kind    0 sample, 1 tone (the 32-byte square wave), 2 noise (the noise buffer and its clone), 0xFF absent (no pointer in the original's table:
                    object 7, or the file is not on the disk; the other fields are 0 except next 0xFF)
    +2  u8  voice   0 to 3 (note 10 1.3)
    +3  u8  volume  the object's default volume (a new object: 0x3F; the wrappers set the start volume)
    +4  u32 offset  first byte of the range in the file (0 for a synthesised object)
    +8  u32 length  bytes of the range: the whole block the hardware plays, replayed from its first byte when it ends (4.2: there is no separate repeat range)
    +12 u16 period  the default period (hardware clocks per sample)
    +14 i8  count   blocks the voice plays of this object before it moves on (4.2); -1 for ever
    +15 u8  reload  the count restored when the voice moves on
    +16 u8  next    the object the voice moves on to when the count runs out; 0xFF none
    +17 u8  script  0 none, 1 tone, 2 noise, 3 gun, 4 launch (note 10 3.1 to 3.5)
    +18 u16 0       reserved
"""
import struct

from .sounds import DEFAULT_PERIOD

OBJECTS = 0x25
RECORD_BYTES = 20
NONE = 0xFF
KIND_SAMPLE, KIND_TONE, KIND_NOISE = 0, 1, 2
SCRIPT_NONE, SCRIPT_TONE, SCRIPT_NOISE, SCRIPT_GUN, SCRIPT_LAUNCH = range(5)
DEFAULT_VOLUME = 0x3F

TONE_BYTES = 32            # 3.2: 16 samples +127 then 16 samples -126
NOISE_BYTES = 0x800        # 3.3
GUN_PERIOD = 150           # 3.5: the gun is a clone of the noise buffer at this period
OBJECT1_BYTES = 5580       # 2.2 rules: object 1 plays the first 5,580 bytes of textcpt
OBJECT3_OFFSET = 0x370     # object 3 starts 880 bytes into textegn and plays to its end
OBJECT9_OFFSET = 0x792     # object 9 starts 1,938 bytes into textwnd; its length is object 3's minus this
# the music pairs (2.2): (even object, file, count, reload, next even object); the odd object is the same on voice 1 and links to next + 1
_MUSIC = [
    (0x0D, "texti0a", 13, 0, 0x0F), (0x0F, "texti0b", 1, 0, 0x11), (0x11, "texti1", 2, 2, 0x13), (0x13, "texti2", 0, 0, 0x15),
    (0x15, "texti1", 2, 2, 0x17), (0x17, "texti2", 0, 0, 0x19), (0x19, "texti3", 2, 2, 0x1B), (0x1B, "texti4", 0, 0, 0x1D),
    (0x1D, "texti3", 2, 2, 0x1F), (0x1F, "texti4", 0, 0, 0x21), (0x21, "texti5", 3, 3, 0x11),
]


def build_sound_table(sound_names, sizes) -> bytes:
    """sound_names: the order of sounds/index.txt; sizes: file name -> length in bytes (the files present on the disk).
    A file that is missing leaves its objects absent (2.3)."""
    index = {n: i for i, n in enumerate(sound_names)}
    recs = [dict(file=NONE, kind=NONE, voice=0, volume=0, offset=0, length=0, period=0, count=0, reload=0, next=NONE, script=SCRIPT_NONE)
            for _ in range(OBJECTS)]

    def sample(obj, name, voice, offset=0, length=None, period=DEFAULT_PERIOD, count=-1, reload=0, nxt=NONE, script=SCRIPT_NONE):
        if name not in sizes or name not in index:
            return
        size = sizes[name]
        offset = min(offset, size)
        length = size - offset if length is None else min(length, size - offset)
        recs[obj] = dict(file=index[name], kind=KIND_SAMPLE, voice=voice, volume=DEFAULT_VOLUME, offset=offset, length=length, period=period,
                         count=count, reload=reload, next=nxt, script=script)

    def synth(obj, kind, length, voice, period, script):
        recs[obj] = dict(file=NONE, kind=kind, voice=voice, volume=DEFAULT_VOLUME, offset=0, length=length, period=period,
                         count=-1, reload=0, next=NONE, script=script)

    sample(0, "textcpt", 0)
    sample(1, "textcpt", 1, length=OBJECT1_BYTES)
    sample(2, "textegn", 0)
    sample(3, "textegn", 1, offset=OBJECT3_OFFSET)
    synth(4, KIND_TONE, TONE_BYTES, 3, DEFAULT_PERIOD, SCRIPT_TONE)
    sample(5, "texttre", 2, count=1)
    synth(6, KIND_NOISE, NOISE_BYTES, 2, DEFAULT_PERIOD, SCRIPT_NOISE)
    sample(8, "textwnd", 0)
    if recs[3]["kind"] == KIND_SAMPLE:   # the loader derives object 9's length from object 3's
        sample(9, "textwnd", 1, offset=OBJECT9_OFFSET, length=max(0, recs[3]["length"] - OBJECT9_OFFSET))
    synth(0x0A, KIND_NOISE, NOISE_BYTES, 2, GUN_PERIOD, SCRIPT_GUN)
    sample(0x0B, "textger", 2)
    if recs[2]["kind"] == KIND_SAMPLE:   # the launch clone needs object 2
        sample(0x0C, "textegn", 2, script=SCRIPT_LAUNCH)
    for even, name, count, reload, nxt in _MUSIC:
        sample(even, name, 0, count=count, reload=reload, nxt=nxt)
        sample(even + 1, name, 1, count=count, reload=reload, nxt=nxt + 1)
    sample(0x23, "text201", 0, period=179)
    sample(0x24, "text201", 1, period=181)
    out = bytearray()
    for r in recs:
        out += struct.pack(">BBBBIIHbBBBH", r["file"], r["kind"], r["voice"], r["volume"], r["offset"], r["length"], r["period"],
                           r["count"], r["reload"], r["next"], r["script"], 0)
    assert len(out) == OBJECTS * RECORD_BYTES
    return bytes(out)

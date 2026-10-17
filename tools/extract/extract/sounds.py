"""The disk's text/ directory holds signed 8-bit mono samples (14 files), not text; the other two files, textply and
textctl, are the demo replay stream (flow.py decode_demo) and are not written here.

The WAV rate of a file is 3,546,895 / period (the PAL colour clock), the period being the reference period of note 10
section 2.4: the period of the file's first play in a flight (a listening copy: the port plays the object table of tables/sounds.bin, not the WAV)."""
import wave

PAL_CLOCK_HZ = 3546895
DEFAULT_PERIOD = 358   # note 10 2.1: the period of a new sound object (the music files, and any file the note does not name)

# note 10 2.4: file name -> reference period (the period of the file's first play in a flight)
REFERENCE_PERIODS = {
    "text201": 179,    # voice 0; voice 1 plays it at 181
    "textcpt": 764,
    "textegn": 776,
    "textegn2": 776,
    "textwnd": 392,
    "texttre": 310,
    "textger": 700,
}


def reference_period(name: str) -> int:
    return REFERENCE_PERIODS.get(name, DEFAULT_PERIOD)


def sample_rate_hz(name: str) -> int:
    p = reference_period(name)
    return (PAL_CLOCK_HZ + p // 2) // p


def write_sample(raw: bytes, raw_path, wav_path, rate_hz: int) -> None:
    with open(raw_path, "wb") as f:
        f.write(raw)
    unsigned = bytes(b ^ 0x80 for b in raw)
    with wave.open(str(wav_path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate_hz)
        w.writeframes(unsigned)

"""The frontend's palette conversion (src/frontend/sdl/video.c, note 06 "Pen 3 and the emulator's colour curve"): the extractor's
RGB is the 4-bit level times 17; the frontend maps each level through the reference emulator's measured curve."""
CURVE = [0, 0, 0, 28, 43, 56, 72, 89, 106, 123, 141, 159, 177, 197, 216, 255]


def convert(raw):
    """The 16 pens (r, g, b) the frontend shows for the bytes of assets/palette/flight.pal."""
    return [tuple(CURVE[raw[3 * i + c] // 17] for c in range(3)) for i in range(16)]

"""python -m extract <image.adf> <assets dir> [--segments <segment table> | --allow-flat-spans]

Copies the game's pictures and sounds from the user's own disk image into a
local assets directory. Nothing produced here may be committed.
"""
import json
import shutil
import sys
import tempfile
from pathlib import Path

from .adf import Adf, AdfError
from .hunk import relocate
from .ilbm import decode_ilbm
from .manifest import EXTRACTOR_VERSION, write_manifest
from .png import write_png
from .cockpit import (ARCS_ADDR, MAPSHAPES_ADDR, MARKER_LARGE_ADDR, MARKER_SMALL_ADDR, MASKS_ADDR, WARNING_ADDR,
                      decode_arcs, decode_mapshapes, decode_markers, decode_masks, decode_warnings)
from .models import decode_model, write_model
from .palette import (FADE_TABLE, FONT_ADDR, decode_fade_table, decode_font, fade_bytes,
                      palette_bytes)
from .sounds import sample_rate_hz, write_sample
from .sound_table import build_sound_table

from .tables import (CAMERA_RING, IMAGE_BASE, LOD_TABLE, RANGE_TABLE, decode_range_table, RECORD_COUNT, RECORD_SIZE, RECORD_TABLE,
                     BlockedError, DecodeError, camera_ring_bytes, decode_camera_ring,
                     decode_lod_table)
from .flight import decode_all as decode_flight_tables, parse_segment_table, reference_longs
from .lists import decode_all as decode_list_tables
from .combat import write_ai, write_burst, write_templates
from .flow import write_menu_font, write_palettes, write_demo, write_scenario, write_text, write_training, DEMO_FILES
from .sky import decode_all as decode_sky_tables
from .terrain import (POSITION_TABLE, decode_position_blocks, decode_terrain, write_terrain)

PICTURES = ["splsh", "inst5", "frnt5"]
EXECUTABLE = "F-18 Interceptor"
DEFAULT_SEGMENTS = Path(__file__).with_name("segments.txt")   # the loader's RAM bases (the same text as tools/extract-js/segments.txt)


def write_models(image: bytes, staging: Path) -> dict:
    """Every model that decodes goes to models/model_NNN.bin; models/index.txt lists all
    ids with a status: ``ok``, ``partial: ...`` (decoded with the BLOCKING items flagged,
    the strict decode having failed with that reason), ``empty: ...`` or ``failed: ...``.
    One failing model never aborts the run."""
    out = staging / "models"
    out.mkdir(parents=True, exist_ok=True)
    lines = ["# id\tfile\tstatus\n"]
    counts = {"ok": 0, "partial": 0, "empty": 0, "failed": 0}
    for i in range(RECORD_COUNT):
        off = RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * i
        fname, status, model = "-", "", None
        try:
            model = decode_model(image, off, i)
            status = "ok"
        except BlockedError as e:
            try:
                model = decode_model(image, off, i, strict=False)
                status = f"partial: {e} ({model.blocked} items flagged blocked)"
            except DecodeError as e2:
                status = f"failed: {e2}"
        except DecodeError as e:
            status = f"empty: {e}" if "empty record" in str(e) else f"failed: {e}"
        kind = status.split(":")[0].split(" ")[0]
        counts[kind] += 1
        if model is not None:
            fname = f"model_{i:03d}.bin"
            (out / fname).write_bytes(write_model(model))
        lines.append(f"{i}\t{fname}\t{status.replace(chr(10), ' ')}\n")
    (out / "index.txt").write_text("".join(lines))
    return counts


def write_world(image: bytes, staging: Path, longs=()) -> dict:
    """Palette, font, tables and terrain from the relocated image. Returns summary counts."""
    def put(rel, data):
        p = staging / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, str):
            p.write_text(data)
        else:
            p.write_bytes(data)

    off = lambda addr: addr - IMAGE_BASE
    fade = decode_fade_table(image, off(FADE_TABLE))
    put("palette/fade.pal", fade_bytes(fade))
    put("palette/flight.pal", palette_bytes(fade[0]))
    put("fonts/hud.bin", b"".join(decode_font(image, off(FONT_ADDR))))
    put("tables/range.bin", decode_range_table(image, off(RANGE_TABLE)))
    put("tables/lod.bin", decode_lod_table(image, off(LOD_TABLE)))
    for name, addr in CAMERA_RING.items():
        tag = {"default": "default", "class11": "11", "class14": "14", "class30": "30"}[name]
        put(f"tables/camera_ring_{tag}.bin", camera_ring_bytes(decode_camera_ring(image, off(addr), name)))
    for fname, data in decode_flight_tables(image, IMAGE_BASE, longs).items():
        put(f"tables/{fname}", data)   # the flight step's tables (note 05 "Extractor needs")
    for fname, data in decode_list_tables(image, IMAGE_BASE).items():
        put(f"tables/{fname}", data)   # the display-list builder's tables (note 04a)
    for fname, data in decode_sky_tables(image, IMAGE_BASE).items():
        put(f"tables/{fname}", data)   # the sky fill's zone tables and the sea mesh pass's tables (note 04)
    write_palettes(image, staging)     # menu.pal, flash.pal (note 02 section 11)
    write_scenario(image, staging)     # tables/scenario.bin (note 02 section 9, note 05 17.14 and 17.17 rule 5)
    write_training(image, staging)     # tables/training.bin (note 08 8.13)
    write_templates(image, staging)    # tables/templates.bin (note 02 section 1)
    write_ai(image, staging)           # tables/ai.bin (note 07 sections 3.6, 4.5, 6.1, 6.3)
    write_burst(image, staging)        # tables/burst.bin, target_text.bin, months.bin (note 07 11.1, 12.8; note 08 10.9)
    write_text(image, staging)         # tables/text.bin, text_ids.bin (note 08 section 2.3)
    write_menu_font(image, staging)    # fonts/menu5x7.bin (note 08 section 3.2)
    write_models(image, staging)
    terrain = decode_terrain(image)
    put("terrain/terrain.bin", write_terrain(terrain))
    lines = [f"table {n} cells {len(c)} grid {g} cell_shift {s}\n" for n, s, g, c in terrain.tables]
    lines.append(f"lists {len(terrain.lists)} items {sum(len(l) for l in terrain.lists)}\n")
    put("terrain/index.txt", "".join(lines))
    put("cockpit/warnings.bin", decode_warnings(image, off(WARNING_ADDR)))
    put("cockpit/masks.bin", decode_masks(image, off(MASKS_ADDR)))
    put("cockpit/markers.bin", decode_markers(image, off(MARKER_SMALL_ADDR), off(MARKER_LARGE_ADDR)))
    put("cockpit/arcs.bin", decode_arcs(image, off(ARCS_ADDR)))
    put("cockpit/mapshapes.bin", decode_mapshapes(image, off(MAPSHAPES_ADDR)))
    blocks = decode_position_blocks(image, POSITION_TABLE, IMAGE_BASE)
    put("terrain/positions.txt", "".join(
        f"{b.ref // 2} {b.cell_x} {b.cell_z} {b.fine_x} {b.fine_z} {b.height}\n" for b in blocks))
    return {"terrain_cells": {n: len(c) for n, s, g, c in terrain.tables}, "position_blocks": len(blocks)}


def extract(adf_path: Path, out_dir: Path, segments: Path = None, allow_flat_spans: bool = False) -> None:
    """segments: the loader's segment table of the reference machine (see extract/flight.py); default the table shipped with the package
    (extract/segments.txt, the RAM bases only; the same text as tools/extract-js/segments.txt). With allow_flat_spans and no table named, the
    junk spans of the flight tables keep the flat values of their relocated longs (1,348 + 12 bytes differ from the reference machine's): the
    extraction writes tables/flight_spans_flat.txt and "spans_relocated": false in the manifest, and the core refuses to load such assets
    (the synthetic disk of the tests, which has no loader table)."""
    if segments is None and not allow_flat_spans:
        segments = DEFAULT_SEGMENTS
    disk = Adf(adf_path)
    try:
        out_dir.parent.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix="interceptor-assets-", dir=out_dir.parent))
        try:
            (staging / "pix").mkdir()
            for name in PICTURES:
                img = decode_ilbm(disk.read(f"pix/{name}"))
                write_png(staging / "pix" / f"{name}.png", img.width, img.height, img.pixels, img.palette)
                (staging / "pix" / f"{name}.palette.json").write_text(
                    json.dumps([list(c) for c in img.palette]) + "\n")
                # raw picture indices (one byte per pixel, planes combined): the cockpit draws pens, not RGB
                (staging / "pix" / f"{name}.idx").write_bytes(bytes(img.pixels))
            (staging / "sounds").mkdir()
            sound_names = [n for n in disk.listdir("text") if n not in DEMO_FILES]   # textply, textctl are the demo stream
            sizes = {}
            for name in sound_names:
                raw = disk.read(f"text/{name}")
                sizes[name] = len(raw)
                write_sample(raw, staging / "sounds" / f"{name}.raw", staging / "sounds" / f"{name}.wav", sample_rate_hz(name))
            write_demo(disk.read("text/textply"), disk.read("text/textctl"), staging)
            exe = disk.read(EXECUTABLE)
            image, _info = relocate(exe, IMAGE_BASE)
            longs = ()
            if segments is not None:
                longs = reference_longs(exe, IMAGE_BASE, parse_segment_table(Path(segments).read_text()))
            else:
                print("warning: no segment table: the junk spans of the flight tables keep flat values; the core will refuse these assets", file=sys.stderr)
                flat = staging / "tables"
                flat.mkdir(parents=True, exist_ok=True)
                (flat / "flight_spans_flat.txt").write_text("the junk spans were extracted without the segment table (manifest spans_relocated: false)\n")
            write_world(image, staging, longs)
            (staging / "tables" / "sounds.bin").write_bytes(build_sound_table(sound_names, sizes))   # note 10 2.2 (extractor version 25)
            (staging / "pix" / "index.txt").write_text("".join(f"{n}\n" for n in PICTURES))
            (staging / "sounds" / "index.txt").write_text(
                "".join(f"{n}\n" for n in sound_names))
            (staging / "VERSION").write_text(f"{EXTRACTOR_VERSION}\n")
            write_manifest(staging, adf_path, {n: sample_rate_hz(n) for n in sound_names}, spans_relocated=segments is not None)
        except BaseException:
            shutil.rmtree(staging, ignore_errors=True)
            raise
        if out_dir.exists():
            shutil.rmtree(out_dir)
        staging.rename(out_dir)
    finally:
        disk.close()


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    segments = None
    allow_flat = "--allow-flat-spans" in argv
    argv = [a for a in argv if a != "--allow-flat-spans"]
    if len(argv) == 4 and argv[2] == "--segments":
        segments, argv = Path(argv[3]), argv[:2]
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    adf_path, out_dir = Path(argv[0]), Path(argv[1])
    if not adf_path.is_file():
        print(f"error: {adf_path} does not exist", file=sys.stderr)
        return 1
    try:
        extract(adf_path, out_dir, segments, allow_flat)
    except (AdfError, ValueError, OSError) as e:   # DecodeError is a ValueError
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"assets written to {out_dir}")
    return 0

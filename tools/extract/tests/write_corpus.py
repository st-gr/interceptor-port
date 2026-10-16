"""Writes the differential corpus of the JavaScript extractor (tools/extract-js/test/corpus.test.js).

    python tools/extract/tests/write_corpus.py tools/extract-js/test/fixtures/corpus.json

The Python decoders are wrapped so that every call a test of tools/extract/tests makes is recorded with its arguments (the
tests build invented images: nothing here comes from the game) and its outcome: a SHA-256 of the canonical result, or the
exception text. The JavaScript test replays each call on the same arguments and demands the same digest or the same message.
Images are stored sparsely (runs of non-zero bytes). Tests that need pytest fixtures, the real assets or subprocesses are
skipped; the generator refuses to run when extracted/ or assets/ hold data, so that nothing of the game can enter the file."""
import hashlib
import importlib
import inspect
import json
import re
import sys
from pathlib import Path

EXTRACT_DIR = Path(__file__).resolve().parents[1]      # tools/extract
sys.path.insert(0, str(EXTRACT_DIR))

REPO = EXTRACT_DIR.parents[1]
for forbidden in (REPO / "extracted", REPO / "assets"):
    if forbidden.exists():
        sys.exit(f"refusing to run: {forbidden} exists (run in a checkout without the game's data, e.g. a clean worktree)")

CALLS = []
IMAGES = {}
_depth = 0
RUN = re.compile(rb"[^\x00]+(?:\x00{1,24}[^\x00]+)*")


def hexs(b):
    return bytes(b).hex()


def canon(x):
    return json.dumps(x, sort_keys=True, separators=(",", ":"))


# ---- normalisers: the result of a decoder as JSON-able data, the same shape the JavaScript adapters build ----
def n_ilbm(r):
    return [r.width, r.height, r.nplanes, [list(c) for c in r.palette], hexs(r.pixels)]


def n_hunk_parse(r):
    sizes, types, contents, relocs, symbols = r
    return [sizes, types, [hexs(c) for c in contents], [[list(p) for p in rl] for rl in relocs], [[list(s) for s in sl] for sl in symbols]]


def n_hunk_relocate(r):
    image, info = r
    return [hexs(image), info["reloc_entries"], info["reloc_count"], [[h["index"], h["type"], h["addr"], h["size"]] for h in info["hunks"]],
            [[s["name"], s["addr"], s["hunk"]] for s in info["symbols"]]]


def n_dict_hex(r):
    return {k: hexs(v) for k, v in r.items()}


def n_pair_hex(r):
    return [hexs(x) for x in r]


def n_terrain(t):
    from extract.terrain import write_terrain
    return hexs(write_terrain(t))


def n_model(m):
    from extract.models import write_model
    return hexs(write_model(m))


def n_record(r):
    return [r.id, r.handler, r.base, r.detail_list, r.alt_list, r.aux]


def n_blocks(bl):
    return [[b.ref, b.cell_x, b.cell_z, b.fine_x, b.fine_z, b.height] for b in bl]


def n_items(il):
    return [[i.layer, i.obj, i.dx, i.dz] for i in il]


def n_seg_table(d):
    return {str(k): list(v) for k, v in sorted(d.items())}


def n_listlist(r):
    return json.loads(json.dumps(r))


NORMALISERS = {
    "tables.decode_lod_table": hexs, "tables.decode_range_table": hexs, "tables.decode_table_bytes": hexs,
    "tables.decode_camera_ring": lambda r: [list(e) for e in r], "tables.decode_record": n_record,
    "palette.decode_fade_table": lambda r: [[list(c) for c in s] for s in r], "palette.decode_font": lambda r: [hexs(g) for g in r],
    "ilbm.decode_ilbm": n_ilbm, "ilbm.unpack_byterun1": hexs,
    "hunk.parse": n_hunk_parse, "hunk.relocate": n_hunk_relocate,
    "sky.decode_all": n_dict_hex,
    "cockpit.decode_warnings": hexs, "cockpit.decode_masks": hexs, "cockpit.decode_markers": hexs, "cockpit.decode_arcs": hexs,
    "cockpit.decode_mapshapes": hexs,
    "flight.decode_all": n_dict_hex, "flight.parse_segment_table": n_seg_table,
    "flight.reference_longs": lambda r: [list(x) for x in r],
    "lists.decode_all": n_dict_hex, "lists.decode_pattern_lists": lambda r: [[[t, list(s)] for t, s in e] for e in r],
    "terrain.decode_terrain": n_terrain, "terrain.decode_item_list": n_items,
    "terrain.decode_cell_table": lambda r: [list(c) for c in r], "terrain.decode_position_blocks": n_blocks,
    "terrain.decode_default_offsets": lambda r: [list(c) for c in r],
    "combat.decode_templates": hexs, "combat.decode_ai": hexs, "combat.decode_burst": hexs, "combat.decode_target_text": hexs,
    "combat.decode_prompts": hexs, "combat.decode_months": hexs, "combat.decode_star": hexs, "combat.decode_briefing_chain": hexs,
    "combat.decode_head_step": hexs,
    "flow.decode_scenario": hexs, "flow.decode_text": n_pair_hex, "flow.decode_menu_font": hexs, "flow.decode_demo": hexs,
    "flow.decode_training": hexs, "flow.palette_words": hexs,
    "models.decode_model": n_model,
}


def store_image(b):
    b = bytes(b)
    h = hashlib.sha256(b).hexdigest()[:20]
    if h not in IMAGES:
        IMAGES[h] = {"len": len(b), "runs": [[m.start(), m.group().hex()] for m in RUN.finditer(b)]}
    return h


class Mutated(bytes):
    """A base image with a few bytes changed (and optionally cut short): recorded as the base plus the changes."""
    base_hash = None
    muts = ()
    cut = None


def mutated(base_hash, base, muts, cut=None):
    b = bytearray(base)
    for off, val in muts:
        b[off] = val
    m = Mutated(bytes(b if cut is None else b[:cut]))
    m.base_hash, m.muts, m.cut = base_hash, [list(x) for x in muts], cut
    return m


def encode_arg(v):
    if isinstance(v, Mutated):
        d = {"image": v.base_hash, "mut": v.muts}
        if v.cut is not None:
            d["cut"] = v.cut
        return d
    if isinstance(v, (bytes, bytearray, memoryview)):
        return {"image": store_image(v)}
    if v is None or isinstance(v, (bool, int, str)):
        return v
    if isinstance(v, (list, tuple)):
        return [encode_arg(x) for x in v]
    if isinstance(v, dict):
        return {"dict": [[encode_arg(k), encode_arg(x)] for k, x in v.items()]}
    raise TypeError(type(v))


def install():
    global _depth
    for key, norm in NORMALISERS.items():
        modname, fname = key.split(".")
        mod = importlib.import_module(f"extract.{modname}")
        fn = getattr(mod, fname)
        sig = inspect.signature(fn)

        def make(fn=fn, sig=sig, key=key, norm=norm):
            def wrapper(*a, **k):
                global _depth
                if _depth:
                    return fn(*a, **k)
                _depth += 1
                try:
                    try:
                        bound = sig.bind(*a, **k)
                        bound.apply_defaults()
                        args = [encode_arg(v) for v in bound.arguments.values()]
                    except TypeError:
                        args = None
                    try:
                        result = fn(*a, **k)
                    except Exception as e:
                        if args is not None:
                            kind = {"DecodeError": "decode", "BlockedError": "blocked", "ValueError": "decode"}.get(type(e).__name__, "other")
                            CALLS.append({"fn": key, "args": args, "err": [kind, str(e)]})
                        raise
                    if args is not None:
                        digest = hashlib.sha256(canon(norm(result)).encode()).hexdigest()
                        CALLS.append({"fn": key, "args": args, "ok": digest})
                    return result
                finally:
                    _depth -= 1
            wrapper.__name__ = fn.__name__
            return wrapper

        setattr(mod, fname, make())


def run_tests():
    names = ["test_burst_text", "test_cockpit", "test_combat", "test_flight_tables", "test_flow", "test_ilbm", "test_lists",
             "test_models", "test_palette", "test_sky", "test_synth_note", "test_tables", "test_terrain"]
    count = 0
    for name in names:
        mod = importlib.import_module(f"tests.{name}")
        for fname, fn in sorted(vars(mod).items()):
            if not fname.startswith("test_") or not callable(fn) or inspect.signature(fn).parameters:
                continue
            try:
                fn()
            except BaseException as e:    # failing or skipped tests still left their calls behind
                if isinstance(e, KeyboardInterrupt):
                    raise
            count += 1
    return count


# ---- fuzzing: mutate a valid synthetic image around the tables a decoder reads, so that its refusals are exercised too ----
def fuzz_tables(rnd, per_family=120):
    from extract import combat as C, flight as FL, flow as F, lists as L, palette as P, sky as S, tables as TB, terrain as T
    from extract.hunk import relocate
    from extract.tables import IMAGE_BASE, RECORD_TABLE
    from tests.synth import flight_patches, make_hunk_exe
    base_img, _ = relocate(make_hunk_exe(0x3C000, flight_patches()), IMAGE_BASE)
    bh = store_image(base_img)
    o = lambda a: a - IMAGE_BASE
    families = [
        (lambda m: C.decode_templates(m), [(RECORD_TABLE, 60)]),
        (lambda m: C.decode_ai(m), [(a, 24) for a in C.AI_RATE_ADDRS] + [(C.AI_EVASION, 40), (C.AI_OFFSETS, 30), (C.AI_RING3, 30),
                                    (C.AI_START, 40), (C.AI_CAM_POINTS, 44), (C.AI_FINE, 20), (C.AI_PRE_WORD, 4), (C.AI_RUN_TABLE, 140),
                                    (C.AI_HULL_PTRS, 14)] + [(a, 44) for a in C.AI_HULL_LISTS]),
        (lambda m: C.decode_burst(m), [(C.BURST_PTRS, 32), (C.BURST_SHAPES, 40), (C.BURST_DISCS, 30), (C.BURST_CYCLE, 64)]
                                      + [(a, 12) for a in C.BURST_PENS] + [(a, 38) for a in C.BURST_OFFSETS]),
        (lambda m: C.decode_target_text(m), [(C.TARGET_BLANK, 28), (C.TARGET_LABELS, 16), (C.TARGET_NAMES, 58)]),
        (lambda m: C.decode_prompts(m), [(C.PROMPTS[0], 14)]),
        (lambda m: C.decode_months(m), [(C.MONTHS_ADDR, 38)]),
        (lambda m: C.decode_star(m), [(C.STAR_ADDR, 38)]),
        (lambda m: C.decode_briefing_chain(m), [(C.CHAIN_ADDR, 64)]),
        (lambda m: C.decode_head_step(m), [(C.HEAD_STEP_ADDR, 16)]),
        (lambda m: F.decode_scenario(m), [(F.START_BLOCKS, 60), (F.REGION_TABLE, 18), (0x2DE3A, 120), (F.PLACEMENT_TABLE, 220)]),
        (lambda m: F.decode_text(m), [(F.TEXT_ID_TABLE, 40), (F.TEXT_RECORDS, 24)]),
        (lambda m: F.decode_menu_font(m), [(F.FONT_TABLE, 130), (F.FONT_BITMAPS, 12)]),
        (lambda m: F.decode_training(m), [(F.TRAINING_A, 66), (F.TRAINING_B, 66), (F.PITCH_RATES, 46)]),
        (lambda m: F.palette_words(m, F.MENU_PALETTE), [(F.MENU_PALETTE, 66)]),
        (lambda m: P.decode_fade_table(m, o(P.FADE_TABLE)), [(P.FADE_TABLE, 40)]),
        (lambda m: P.decode_font(m, o(P.FONT_ADDR)), [(P.FONT_ADDR, 140)]),
        (lambda m: FL.decode_all(m, IMAGE_BASE, ()), [(FL.PAD_TABLE, 84), (FL.SHIP_BLOBS["flight_carrier"][0][0], 130), (FL.SHIP_BLOBS["flight_carrier"][1][0], 50),
                                                      (FL.SHIP_BLOBS["flight_ship2"][0][0], 36), (FL.SHIP_BLOBS["flight_ship2"][1][0], 50)]),
        (lambda m: L.decode_all(m, IMAGE_BASE), [(0x4625C, 20), (0x4628C, 20), (0x462DC, 100), (0x4633A, 16), (0x4659C, 100), (0x4717E, 140), (RECORD_TABLE, 40)]),
        (lambda m: T.decode_terrain(m), [(0x47374, 60), (0x47474, 60), (0x47574, 60), (0x207C6, 34)]),
        (lambda m: TB.decode_lod_table(m, o(TB.LOD_TABLE)), [(TB.LOD_TABLE, 240)]),
        (lambda m: S.decode_all(m, IMAGE_BASE), [(0x23188, 16)]),
    ]
    for call, regions in families:
        for i in range(per_family):
            addr, size = regions[rnd.randrange(len(regions))]
            n = rnd.choice((1, 1, 2, 3))
            at = o(addr) + rnd.randrange(size)
            muts = [(at + k, rnd.choice((0, 1, 0x7F, 0x80, 0xFF, rnd.randrange(256)))) for k in range(n)]
            cut = o(addr) + rnd.randrange(size + 4) if i % 12 == 11 else None
            try:
                call(mutated(bh, base_img, muts, cut))
            except Exception:
                pass


def fuzz_models(rnd, images, per_model=14, limit=1800):
    from extract.models import decode_model
    done = 0
    for im in images:
        data, base = bytes(im.data), im.base
        nz = [i for i, b in enumerate(data) if b]
        if len(data) < 20 * 40 or not nz:
            continue
        h = store_image(data)
        mids = [m for m in range(40) if any(data[20 * m:20 * m + 20])]
        for mid in mids[:2]:
            for _ in range(per_model):
                if done >= limit:
                    return
                r = rnd.random()
                at = rnd.choice(nz) if r < 0.6 else rnd.randrange(max(0, nz[0] - 8), min(len(data), nz[-1] + 8))
                muts = [(at, rnd.choice((0, 1, 0x80, 0xFF, 0x0C, 0x14, rnd.randrange(256))))]
                if r > 0.9:
                    muts.append((min(len(data) - 1, at + 1), rnd.randrange(256)))
                m = mutated(h, data, muts)
                for strict in ((True, False) if rnd.random() < 0.3 else (True,)):
                    try:
                        decode_model(m, 20 * mid, mid, base, strict)
                    except Exception:
                        pass
                    done += 1


def dedupe(calls):
    seen, out = set(), []
    for c in calls:
        key = canon(c)
        if key not in seen:
            seen.add(key)
            out.append(c)
    return out


def main(argv):
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    import random
    import tests.synth as synth
    original_img = synth.Img
    images = []

    class RecordingImg(original_img):
        def __init__(self, *a, **k):
            super().__init__(*a, **k)
            images.append(self)

    synth.Img = RecordingImg
    install()
    n = run_tests()
    rnd = random.Random(23)
    fuzz_tables(rnd)
    fuzz_models(rnd, images)
    calls = dedupe(CALLS)
    # keep the file small: at most 200 calls per function and outcome kind (2,000 for the models)
    kept, per = [], {}
    for c in calls:
        k = (c["fn"], "ok" if "ok" in c else c["err"][0])
        per[k] = per.get(k, 0) + 1
        if per[k] <= (2000 if c["fn"] == "models.decode_model" else 200):
            kept.append(c)
    used = {a["image"] for c in kept for a in _walk(c["args"]) if isinstance(a, dict) and "image" in a}
    out = {"images": {h: IMAGES[h] for h in sorted(used)}, "calls": kept}
    Path(argv[0]).write_text(json.dumps(out, separators=(",", ":")) + "\n")
    print(f"{n} tests run, {len(CALLS)} calls recorded, {len(kept)} kept, {len(used)} images", file=sys.stderr)
    return 0


def _walk(x):
    if isinstance(x, list):
        for v in x:
            yield from _walk(v)
    elif isinstance(x, dict):
        yield x
        if "dict" in x:
            for k, v in x["dict"]:
                yield from _walk(k)
                yield from _walk(v)
    else:
        yield x


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

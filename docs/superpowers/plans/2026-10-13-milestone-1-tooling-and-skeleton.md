# Milestone 1: Tooling and Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up every tool and skeleton the port needs: build system, asset extractor, core API with scene struct, SDL frontend showing the splash, relocated binary for Ghidra, Musashi test rig, headless reference emulator with the AROS ROM, and three-platform CI.

**Architecture:** A pure C99 core library (`src/core`) that never draws; an SDL2 frontend (`src/frontend/sdl`) that renders the core's scene struct; Python tools (`tools/extract`, `tools/hunk`) that turn the user's disk image into git-ignored assets and a relocated flat binary; C tools (`tools/m68k-rig`, `tools/vamiga-ref`) that execute original code for verification. One Makefile drives C builds; pytest drives Python tests.

**Tech Stack:** C99 (gcc, tcc, clang, emcc), SDL2, Python 3 with amitools (hunk and ADF parsing), Musashi (68000 core, git submodule), vAmiga Core (headless Amiga emulator, C++20, CMake), GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-13-interceptor-port-design.md`

## Global Constraints

- Clean room: no disassembly listings, decompiler output or original code in the repository, in notes or in comments.
- Copyright: `disks/`, `extracted/`, `assets/` and `*.rom` are git-ignored and never committed; tests that need them must skip cleanly when they are absent.
- Core compiles with gcc, clang, MSVC, tcc and emcc: no OS calls in `src/core`, fixed-width integer types only, explicit little-endian asset files, no reliance on struct layout or packing.
- tcc is used for iteration on x86_64 Linux and Windows only; macOS uses clang.
- Build is one Makefile: `make CC=tcc` for iteration, `make CC=gcc` for optimised builds, `make web` for Emscripten. No CMake for our own code (vAmiga's upstream CMake is used only to build vAmiga).
- Tick rate, fixed-point formats and numeric game constants come from RE notes, never guessed. Milestone 1 contains no game constants; the one provisional value (sound sample rate, Task 3) is labelled provisional in code and in the manifest.
- Sandbox: `. /sandbox/.tools/env.sh` first in every shell. Install system packages with `micromamba install -p /sandbox/.tools/env -c conda-forge <pkg>`, Python packages with `/sandbox/.tools/env/bin/pip install <pkg>`.
- Commit messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

## Review Focus

Inputs the spec implies but which are easy to break, with the task whose tests pin them:

1. A user runs the extractor on the wrong disk image (another game's ADF, or a non-ADF file). Expected: a clear error naming what is missing, no partial `assets/` left behind. Pinned in Task 3.
2. An ILBM with a mask plane or odd width. Expected: pixels decoded correctly, not shifted. Pinned in Task 2.
3. The core is asked for a scene before any step, or stepped with a null loader. Expected: deterministic defaults and an error string, never a crash. Pinned in Task 1.
4. The SDL frontend starts with `assets/` missing. Expected: a message naming the extractor command and a non-zero exit, not a segfault. Pinned in Task 8.
5. A relocated hunk image where a relocation targets a BSS hunk (zero-filled, no file data). Expected: the address is still patched. Pinned in Task 4.

## Task dependency graph (for parallel execution)

```
Task 1 (skeleton, core API) ──┬── Task 5 (rig, also needs Task 4)
                              ├── Task 8 (SDL frontend) ── Task 9 (tcc + web build)
                              └── Task 10 (CI, also needs Task 3, 8)
Task 2 (ILBM) ── Task 3 (extractor CLI)
Task 4 (hunk relocator) ──┬── Task 5
                          └── Task 6 (Ghidra layout note)
Task 7 (reference emulator) — independent
Task 11 (docs and wrap-up) — last
```

Run Tasks 1, 2, 4, 7 concurrently first. Then 3, 5, 6, 8 concurrently. Then 9, 10. Then 11.

---

### Task 1: Repository skeleton, Makefile, core API and test harness

**Files:**
- Create: `Makefile`
- Create: `src/core/game.h`
- Create: `src/core/scene.h`
- Create: `src/core/game.c`
- Create: `tests/test.h`
- Create: `tests/test_game.c`
- Create: `README.md`

**Interfaces:**
- Consumes: nothing.
- Produces: the core API in `src/core/game.h` and the scene struct in `src/core/scene.h`, exactly as written below. Task 8 renders `game_scene_t`; Task 5 and Task 10 use `make test`.

- [ ] **Step 1: Write the scene header**

```c
/* src/core/scene.h - everything a frontend needs to draw one frame.
 * Plain data, no pointers into the core, no draw calls. */
#ifndef INTERCEPTOR_SCENE_H
#define INTERCEPTOR_SCENE_H
#include <stdint.h>

#define GAME_MAX_OBJECTS 64
#define GAME_MAX_TEXT    32
#define GAME_MAX_SOUNDS  8
#define GAME_TEXT_LEN    48

typedef enum {
    GAME_MODE_SPLASH = 0,
    GAME_MODE_MENU,
    GAME_MODE_BRIEFING,
    GAME_MODE_FLIGHT,
    GAME_MODE_LOG,
    GAME_MODE_DEBRIEF,
    GAME_MODE_PAUSED
} game_mode_t;

typedef enum {
    GAME_VIEW_COCKPIT = 0,
    GAME_VIEW_EXTERNAL,
    GAME_VIEW_TOWER,
    GAME_VIEW_CHASE
} game_view_t;

typedef struct { float x, y, z; } game_vec3_t;

typedef struct {
    game_vec3_t pos;
    float pitch, roll, yaw;   /* radians */
    game_view_t view;
} game_camera_t;

typedef struct {
    uint16_t asset_id;        /* index into assets/models manifest */
    uint16_t variant;         /* animation or damage state */
    game_vec3_t pos;
    float pitch, roll, yaw;
} game_object_t;

typedef struct {
    int32_t airspeed_kt, altitude_ft, heading_deg, vspeed_fpm;
    int32_t throttle_pct, fuel_pct;
    uint8_t gear_down, hook_down;
    uint8_t weapon;           /* 0 gun, 1 AIM-9L, 2 AIM-120 */
    uint16_t gun_rounds, aim9_count, aim120_count, flares, chaff;
    char warning[GAME_TEXT_LEN];
} game_hud_t;

typedef struct {
    int16_t row, col;         /* character cell on a 40x25 text grid */
    char text[GAME_TEXT_LEN];
} game_text_t;

typedef struct {
    uint16_t sample_id;       /* index into assets/sounds manifest */
    uint16_t pitch_q8;        /* playback rate multiplier, 8.8 fixed point, 256 = nominal */
    uint8_t volume;           /* 0..64 like the original hardware */
    uint8_t loop;
} game_sound_t;

typedef struct {
    game_mode_t mode;
    uint32_t tick;            /* frames since game_create */
    game_camera_t camera;
    uint16_t object_count;
    game_object_t objects[GAME_MAX_OBJECTS];
    game_hud_t hud;
    uint16_t text_count;
    game_text_t text[GAME_MAX_TEXT];
    uint16_t sound_count;
    game_sound_t sounds[GAME_MAX_SOUNDS];
    uint16_t splash_asset;    /* which picture to show in GAME_MODE_SPLASH */
} game_scene_t;

#endif
```

- [ ] **Step 2: Write the core API header**

```c
/* src/core/game.h - the only entry points a frontend may use. */
#ifndef INTERCEPTOR_GAME_H
#define INTERCEPTOR_GAME_H
#include <stddef.h>
#include <stdint.h>
#include "scene.h"

typedef struct game game_t;

/* Asset loader supplied by the frontend. read() returns 0 on success and
 * fills *out/*len with a buffer the core later hands back to release(). */
typedef struct {
    int  (*read)(void *user, const char *path, uint8_t **out, size_t *len);
    void (*release)(void *user, uint8_t *buf);
    void *user;
} game_loader_t;

typedef enum { GAME_EV_AXIS = 0, GAME_EV_BUTTON, GAME_EV_KEY } game_event_kind_t;

/* Axis codes */
enum { GAME_AXIS_STICK_X = 0, GAME_AXIS_STICK_Y, GAME_AXIS_THROTTLE, GAME_AXIS_RUDDER };
/* Button codes (value 1 = pressed, 0 = released) */
enum { GAME_BTN_FIRE = 0, GAME_BTN_SELECT_WEAPON, GAME_BTN_GEAR, GAME_BTN_HOOK,
       GAME_BTN_FLARE, GAME_BTN_CHAFF, GAME_BTN_VIEW_NEXT, GAME_BTN_MENU,
       GAME_BTN_ESC, GAME_BTN_RETURN, GAME_BTN_ANY };
/* GAME_EV_KEY: value is an ASCII character for text entry */

typedef struct {
    game_event_kind_t kind;
    int32_t code;
    int32_t value;            /* axes: -32768..32767, buttons: 0/1, keys: ASCII */
} game_event_t;

/* Returns NULL on failure and writes a message into err (if errlen > 0). */
game_t *game_create(const game_loader_t *loader, char *err, size_t errlen);
void    game_destroy(game_t *g);
void    game_input(game_t *g, const game_event_t *ev);
void    game_step(game_t *g);                       /* exactly one original tick */
void    game_scene(const game_t *g, game_scene_t *out);

#endif
```

- [ ] **Step 3: Write the minimal test framework**

```c
/* tests/test.h - tiny test harness, no dependencies. */
#ifndef INTERCEPTOR_TEST_H
#define INTERCEPTOR_TEST_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_failures = 0;
static int test_count = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { test_count++; printf("  %-40s", #name); name(); printf("ok\n"); } while (0)
#define FAIL(msg) do { test_failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); return; } while (0)
#define ASSERT_TRUE(c) do { if (!(c)) FAIL(#c); } while (0)
#define ASSERT_EQ_INT(a, b) do { long _a = (long)(a), _b = (long)(b); \
    if (_a != _b) { printf("  expected %ld got %ld\n", _b, _a); FAIL(#a " == " #b); } } while (0)
#define ASSERT_STR_EQ(a, b) do { if (strcmp((a), (b)) != 0) { printf("  got \"%s\"\n", (a)); FAIL(#a " == " #b); } } while (0)
#define TEST_MAIN_END() do { printf("%d tests, %d failures\n", test_count, test_failures); return test_failures ? 1 : 0; } while (0)

#endif
```

- [ ] **Step 4: Write the failing core tests**

```c
/* tests/test_game.c */
#include "test.h"
#include "../src/core/game.h"

static int fake_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user; (void)path;
    *out = (uint8_t *)malloc(1); *len = 0;
    return 0;
}
static void fake_release(void *user, uint8_t *buf) { (void)user; free(buf); }

TEST(create_with_null_loader_fails_with_message) {
    char err[64] = "";
    game_t *g = game_create(NULL, err, sizeof err);
    ASSERT_TRUE(g == NULL);
    ASSERT_TRUE(strlen(err) > 0);
}

TEST(scene_before_any_step_is_splash_at_tick_zero) {
    game_loader_t ld = { fake_read, fake_release, NULL };
    game_t *g = game_create(&ld, NULL, 0);
    ASSERT_TRUE(g != NULL);
    game_scene_t s;
    memset(&s, 0xAA, sizeof s);
    game_scene(g, &s);
    ASSERT_EQ_INT(s.mode, GAME_MODE_SPLASH);
    ASSERT_EQ_INT(s.tick, 0);
    ASSERT_EQ_INT(s.object_count, 0);
    ASSERT_EQ_INT(s.text_count, 0);
    ASSERT_EQ_INT(s.sound_count, 0);
    game_destroy(g);
}

TEST(step_increments_tick_and_input_is_accepted) {
    game_loader_t ld = { fake_read, fake_release, NULL };
    game_t *g = game_create(&ld, NULL, 0);
    game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_ANY, 1 };
    game_input(g, &ev);
    game_step(g);
    game_step(g);
    game_scene_t s;
    game_scene(g, &s);
    ASSERT_EQ_INT(s.tick, 2);
    game_destroy(g);
}

int main(void) {
    RUN(create_with_null_loader_fails_with_message);
    RUN(scene_before_any_step_is_splash_at_tick_zero);
    RUN(step_increments_tick_and_input_is_accepted);
    TEST_MAIN_END();
}
```

- [ ] **Step 5: Write the Makefile**

```make
# Makefile - one build for core, tests, SDL frontend and web.
CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
BUILD   ?= build

CORE_SRC := $(wildcard src/core/*.c)
CORE_OBJ := $(patsubst src/core/%.c,$(BUILD)/core/%.o,$(CORE_SRC))
TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(patsubst tests/%.c,$(BUILD)/tests/%,$(TEST_SRC))

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)
FRONTEND_SRC := $(wildcard src/frontend/sdl/*.c)

.PHONY: all core test frontend web clean

all: core frontend

core: $(BUILD)/libcore.a

$(BUILD)/libcore.a: $(CORE_OBJ)
	ar rcs $@ $^

$(BUILD)/core/%.o: src/core/%.c src/core/game.h src/core/scene.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/tests/%: tests/%.c $(BUILD)/libcore.a tests/test.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(BUILD)/libcore.a -o $@ -lm

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "$$t"; $$t || exit 1; done

frontend: $(BUILD)/interceptor

$(BUILD)/interceptor: $(FRONTEND_SRC) $(BUILD)/libcore.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FRONTEND_SRC) $(BUILD)/libcore.a -o $@ $(SDL_LIBS) -lm

web: $(BUILD)/web/interceptor.html

$(BUILD)/web/interceptor.html: $(FRONTEND_SRC) $(CORE_SRC)
	@mkdir -p $(dir $@)
	emcc -std=c99 -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 \
	  --preload-file assets@/assets $(CORE_SRC) $(FRONTEND_SRC) -o $@

clean:
	rm -rf $(BUILD)
```

Note the `frontend` and `web` targets fail until Task 8 adds frontend sources; `make core test` works after this task.

- [ ] **Step 6: Run the tests to verify they fail**

Run: `. /sandbox/.tools/env.sh && make core test`
Expected: link error, `game_create` undefined (no `src/core/game.c` yet).

- [ ] **Step 7: Write the minimal core implementation**

```c
/* src/core/game.c - core state machine. Milestone 1: splash only. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "game.h"

struct game {
    game_loader_t loader;
    uint32_t tick;
    game_mode_t mode;
};

static void set_err(char *err, size_t errlen, const char *msg) {
    if (err && errlen) { strncpy(err, msg, errlen - 1); err[errlen - 1] = '\0'; }
}

game_t *game_create(const game_loader_t *loader, char *err, size_t errlen) {
    if (!loader || !loader->read || !loader->release) {
        set_err(err, errlen, "game_create: loader with read and release callbacks is required");
        return NULL;
    }
    game_t *g = (game_t *)calloc(1, sizeof *g);
    if (!g) { set_err(err, errlen, "game_create: out of memory"); return NULL; }
    g->loader = *loader;
    g->mode = GAME_MODE_SPLASH;
    return g;
}

void game_destroy(game_t *g) { free(g); }

void game_input(game_t *g, const game_event_t *ev) {
    (void)g; (void)ev;   /* milestone 1: inputs are accepted and ignored */
}

void game_step(game_t *g) { g->tick++; }

void game_scene(const game_t *g, game_scene_t *out) {
    memset(out, 0, sizeof *out);
    out->mode = g->mode;
    out->tick = g->tick;
    out->splash_asset = 0;
}
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `make core test`
Expected: `3 tests, 0 failures`.

- [ ] **Step 9: Write README.md**

```markdown
# interceptor-wasm

Clean-room re-implementation of *F/A-18 Interceptor* (Electronic Arts, 1988,
Amiga). Design: `docs/superpowers/specs/2026-10-13-interceptor-port-design.md`.

## Build

    make core test          # core library and unit tests (any C99 compiler)
    make CC=tcc core test   # fastest iteration
    make                    # plus the SDL2 frontend: build/interceptor
    make web                # Emscripten build: build/web/interceptor.html

## Assets

The game's data is copyrighted and not in this repository. Decode your own
disk image to an ADF and run the extractor (see `tools/extract/README.md`).
```

- [ ] **Step 10: Commit**

```bash
git add Makefile src/core tests README.md
git commit -m "Core API, scene struct, test harness and Makefile skeleton

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: ILBM decoder and PNG writer (Python)

**Files:**
- Create: `tools/extract/pyproject.toml`
- Create: `tools/extract/extract/__init__.py` (empty)
- Create: `tools/extract/extract/ilbm.py`
- Create: `tools/extract/extract/png.py`
- Create: `tools/extract/tests/test_ilbm.py`
- Create: `tools/extract/tests/test_png.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `extract.ilbm.decode_ilbm(data: bytes) -> Ilbm` with fields `width`, `height`, `nplanes`, `palette: list[tuple[int,int,int]]`, `pixels: bytes` (one byte per pixel, row-major); `extract.ilbm.unpack_byterun1(src: bytes, out_len: int) -> bytes`; `extract.png.write_png(path, width, height, pixels, palette)` writing an indexed 8-bit PNG. Task 3 calls both.

- [ ] **Step 1: Create the package**

`tools/extract/pyproject.toml`:
```toml
[project]
name = "interceptor-extract"
version = "0.1.0"
description = "Extracts F/A-18 Interceptor assets from the user's own ADF into assets/"
requires-python = ">=3.10"
dependencies = ["amitools>=0.8"]

[project.optional-dependencies]
test = ["pytest"]

[build-system]
requires = ["setuptools>=61"]
build-backend = "setuptools.build_meta"

[tool.setuptools.packages.find]
where = ["."]
include = ["extract*"]
```

Create an empty `tools/extract/extract/__init__.py`.

- [ ] **Step 2: Write the failing ILBM tests**

```python
# tools/extract/tests/test_ilbm.py
import struct
import pytest
from extract.ilbm import decode_ilbm, unpack_byterun1


def make_ilbm(width, height, nplanes, planes_rows, palette, compression=0, masking=0):
    """planes_rows: list over rows of list over planes of row bytes (already packed)."""
    bmhd = struct.pack(">HHhhBBBBHBBhh", width, height, 0, 0, nplanes, masking,
                       compression, 0, 0, 10, 11, width, height)
    cmap = b"".join(bytes(c) for c in palette)
    body = b"".join(b"".join(row) for row in planes_rows)

    def chunk(cid, data):
        return cid + struct.pack(">I", len(data)) + data + (b"\0" if len(data) & 1 else b"")

    inner = b"ILBM" + chunk(b"BMHD", bmhd) + chunk(b"CMAP", cmap) + chunk(b"BODY", body)
    return b"FORM" + struct.pack(">I", len(inner)) + inner


def test_decodes_two_plane_uncompressed_image():
    # 16x2, two planes. Row 0: plane0 = 0b1010..., plane1 = 0b1100...
    rows = [
        [bytes([0b10101010, 0]), bytes([0b11001100, 0])],
        [bytes([0, 0]), bytes([0, 0])],
    ]
    data = make_ilbm(16, 2, 2, rows, [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)])
    img = decode_ilbm(data)
    assert (img.width, img.height, img.nplanes) == (16, 2, 2)
    assert img.palette == [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    # pixel x: bit from plane0 is bit0, from plane1 is bit1
    assert list(img.pixels[:8]) == [3, 2, 1, 0, 3, 2, 1, 0]
    assert list(img.pixels[16:24]) == [0] * 8


def test_mask_plane_is_skipped_and_odd_width_rows_are_word_aligned():
    # width 9 -> row bytes = 2 (padded to a word). masking=1 adds a mask plane per row.
    rows = [[bytes([0xFF, 0x80]), bytes([0xFF, 0xFF])]]  # plane0 then mask
    data = make_ilbm(9, 1, 1, rows, [(0, 0, 0), (255, 255, 255)], masking=1)
    img = decode_ilbm(data)
    assert img.width == 9 and len(img.pixels) == 9
    assert list(img.pixels) == [1] * 9


def test_byterun1_literal_and_repeat_runs():
    # n=2 -> copy 3 literal bytes; n=0xFE (254) -> repeat next byte 257-254=3 times; 0x80 no-op
    src = bytes([2, 10, 20, 30, 0xFE, 7, 0x80])
    assert unpack_byterun1(src, 6) == bytes([10, 20, 30, 7, 7, 7])


def test_compressed_body_is_unpacked():
    # one row, one plane, width 16 -> 2 bytes per row, encoded as a 2-byte literal run
    body_rows = [[bytes([1, 0xAA, 0x55])]]
    data = make_ilbm(16, 1, 1, body_rows, [(0, 0, 0), (9, 9, 9)], compression=1)
    img = decode_ilbm(data)
    assert list(img.pixels) == [1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1]


def test_rejects_non_ilbm():
    with pytest.raises(ValueError):
        decode_ilbm(b"RIFF\0\0\0\0WAVE")
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cd tools/extract && /sandbox/.tools/env/bin/pip install -e ".[test]" && pytest tests/test_ilbm.py -q`
Expected: ImportError, `extract.ilbm` not found.

- [ ] **Step 4: Write the decoder**

```python
# tools/extract/extract/ilbm.py
"""IFF ILBM decoder (Electronic Arts Interchange File Format, 1985).

Only what the game's pictures need: BMHD, CMAP, BODY, ByteRun1 compression,
optional mask plane. Output is one byte per pixel, row-major.
"""
from dataclasses import dataclass
import struct


@dataclass
class Ilbm:
    width: int
    height: int
    nplanes: int
    palette: list
    pixels: bytes


def _chunks(data: bytes, start: int, end: int):
    pos = start
    while pos + 8 <= end:
        cid = data[pos:pos + 4]
        (length,) = struct.unpack(">I", data[pos + 4:pos + 8])
        yield cid, data[pos + 8:pos + 8 + length]
        pos += 8 + length + (length & 1)


def unpack_byterun1(src: bytes, out_len: int) -> bytes:
    out = bytearray()
    i = 0
    while len(out) < out_len and i < len(src):
        n = src[i]
        i += 1
        if n < 128:
            out += src[i:i + n + 1]
            i += n + 1
        elif n > 128:
            out += bytes([src[i]]) * (257 - n)
            i += 1
        # n == 128 is a no-op by definition
    return bytes(out[:out_len])


def decode_ilbm(data: bytes) -> Ilbm:
    if len(data) < 12 or data[:4] != b"FORM" or data[8:12] != b"ILBM":
        raise ValueError("not an IFF ILBM picture")
    (form_len,) = struct.unpack(">I", data[4:8])
    bmhd = cmap = body = None
    for cid, chunk in _chunks(data, 12, 8 + form_len):
        if cid == b"BMHD":
            bmhd = chunk
        elif cid == b"CMAP":
            cmap = chunk
        elif cid == b"BODY":
            body = chunk
    if bmhd is None or body is None:
        raise ValueError("ILBM is missing BMHD or BODY")
    (w, h, _x, _y, nplanes, masking, compression, _pad, _transp,
     _xa, _ya, _pw, _ph) = struct.unpack(">HHhhBBBBHBBhh", bmhd[:20])
    row_bytes = ((w + 15) // 16) * 2
    rows_per_pixel_row = nplanes + (1 if masking == 1 else 0)
    needed = row_bytes * rows_per_pixel_row * h
    raw = body if compression == 0 else unpack_byterun1(body, needed)
    if len(raw) < needed:
        raise ValueError("ILBM body is truncated")
    pixels = bytearray(w * h)
    pos = 0
    for row in range(h):
        base = row * w
        for plane in range(rows_per_pixel_row):
            plane_row = raw[pos:pos + row_bytes]
            pos += row_bytes
            if plane >= nplanes:
                continue  # mask plane
            bit = 1 << plane
            for x in range(w):
                if plane_row[x >> 3] & (0x80 >> (x & 7)):
                    pixels[base + x] |= bit
    palette = []
    if cmap:
        palette = [(cmap[i], cmap[i + 1], cmap[i + 2]) for i in range(0, len(cmap) - 2, 3)]
    return Ilbm(w, h, nplanes, palette, bytes(pixels))
```

- [ ] **Step 5: Run the ILBM tests to verify they pass**

Run: `pytest tests/test_ilbm.py -q`
Expected: 5 passed.

- [ ] **Step 6: Write the failing PNG test**

```python
# tools/extract/tests/test_png.py
import struct
import zlib
from extract.png import write_png


def test_writes_indexed_png_that_round_trips(tmp_path):
    path = tmp_path / "t.png"
    pixels = bytes([0, 1, 2, 3, 3, 2, 1, 0])
    palette = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    write_png(path, 4, 2, pixels, palette)
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    # parse chunks
    pos = 8
    chunks = {}
    while pos < len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4])
        cid = data[pos + 4:pos + 8]
        chunks[cid] = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
    w, h, depth, ctype = struct.unpack(">IIBB", chunks[b"IHDR"][:10])
    assert (w, h, depth, ctype) == (4, 2, 8, 3)
    assert chunks[b"PLTE"] == bytes([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255])
    raw = zlib.decompress(chunks[b"IDAT"])
    assert raw == b"\x00" + pixels[:4] + b"\x00" + pixels[4:]
```

- [ ] **Step 7: Run it to verify it fails**

Run: `pytest tests/test_png.py -q`
Expected: ImportError.

- [ ] **Step 8: Write the PNG writer**

```python
# tools/extract/extract/png.py
"""Minimal indexed-colour PNG writer. No dependencies beyond zlib."""
import struct
import zlib


def _chunk(cid: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + cid + data
            + struct.pack(">I", zlib.crc32(cid + data) & 0xFFFFFFFF))


def write_png(path, width: int, height: int, pixels: bytes, palette) -> None:
    if len(pixels) != width * height:
        raise ValueError("pixel buffer size does not match width*height")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(c) for c in palette)
    rows = b"".join(b"\x00" + pixels[y * width:(y + 1) * width] for y in range(height))
    idat = zlib.compress(rows, 9)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", ihdr))
        f.write(_chunk(b"PLTE", plte))
        f.write(_chunk(b"IDAT", idat))
        f.write(_chunk(b"IEND", b""))
```

- [ ] **Step 9: Run all extractor tests to verify they pass**

Run: `pytest -q`
Expected: 6 passed.

- [ ] **Step 10: Commit**

```bash
git add tools/extract
git commit -m "Extractor: ILBM decoder and indexed PNG writer

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Extractor CLI: ADF reader, pictures, sounds, manifest

**Files:**
- Create: `tools/extract/extract/adf.py`
- Create: `tools/extract/extract/sounds.py`
- Create: `tools/extract/extract/manifest.py`
- Create: `tools/extract/extract/cli.py`
- Create: `tools/extract/extract/__main__.py`
- Create: `tools/extract/tests/test_cli.py`
- Create: `tools/extract/README.md`

**Interfaces:**
- Consumes: `decode_ilbm`, `write_png` from Task 2.
- Produces: the `assets/` layout read by Task 8 and later milestones:
  - `assets/manifest.json`: `{"extractor_version": 1, "source_sha256": "<hex>", "sample_rate_hz": 8000, "sample_rate_provisional": true, "files": {"<relative path>": "<sha256 hex>"}}`
  - `assets/pix/<name>.png` and `assets/pix/<name>.palette.json` (`[[r,g,b], ...]`) for `splsh`, `inst5`, `frnt5`
  - `assets/sounds/<name>.wav` (8-bit unsigned mono) and `assets/sounds/<name>.raw` (original signed 8-bit bytes) for each file in `text/`
- CLI: `python -m extract <image.adf> <assets dir>`; exit 0 on success, 1 with a message on error, and no partial output directory on error.

- [ ] **Step 1: Write the failing CLI test with a synthetic ADF**

```python
# tools/extract/tests/test_cli.py
import json
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

import pytest

from tests.test_ilbm import make_ilbm

XDFTOOL = shutil.which("xdftool")
pytestmark = pytest.mark.skipif(XDFTOOL is None, reason="xdftool (amitools) not on PATH")


def build_adf(tmp_path, with_pix=True):
    ilbm = make_ilbm(16, 1, 1, [[bytes([0xF0, 0x0F])]], [(0, 0, 0), (255, 255, 255)])
    (tmp_path / "splsh").write_bytes(ilbm)
    (tmp_path / "textegn").write_bytes(bytes([0, 127, 255, 128]))  # signed: 0, 127, -1, -128
    adf = tmp_path / "t.adf"
    cmd = [XDFTOOL, str(adf), "create", "+", "format", "Test", "+",
           "makedir", "text", "+", "write", str(tmp_path / "textegn"), "text/textegn"]
    if with_pix:
        cmd += ["+", "makedir", "pix", "+", "write", str(tmp_path / "splsh"), "pix/splsh"]
    subprocess.run(cmd, check=True, capture_output=True)
    return adf


def run_extract(adf, out):
    return subprocess.run([sys.executable, "-m", "extract", str(adf), str(out)],
                          capture_output=True, text=True)


def test_extracts_pictures_sounds_and_manifest(tmp_path):
    adf = build_adf(tmp_path)
    out = tmp_path / "assets"
    r = run_extract(adf, out)
    assert r.returncode == 0, r.stderr
    assert (out / "pix" / "splsh.png").exists()
    assert json.loads((out / "pix" / "splsh.palette.json").read_text()) == [[0, 0, 0], [255, 255, 255]]
    assert (out / "sounds" / "textegn.raw").read_bytes() == bytes([0, 127, 255, 128])
    with wave.open(str(out / "sounds" / "textegn.wav")) as w:
        assert (w.getnchannels(), w.getsampwidth()) == (1, 1)
        assert w.readframes(4) == bytes([128, 255, 127, 0])  # signed -> unsigned
    m = json.loads((out / "manifest.json").read_text())
    assert m["extractor_version"] == 1
    assert m["sample_rate_provisional"] is True
    assert set(m["files"]) == {"pix/splsh.png", "pix/splsh.palette.json",
                               "sounds/textegn.wav", "sounds/textegn.raw"}


def test_wrong_disk_leaves_no_output_and_names_missing_file(tmp_path):
    adf = build_adf(tmp_path, with_pix=False)
    out = tmp_path / "assets"
    r = run_extract(adf, out)
    assert r.returncode == 1
    assert "pix/splsh" in r.stderr
    assert not out.exists()


def test_non_adf_file_is_rejected(tmp_path):
    bogus = tmp_path / "x.adf"
    bogus.write_bytes(b"not a disk")
    r = run_extract(bogus, tmp_path / "assets")
    assert r.returncode == 1
    assert "ADF" in r.stderr or "disk" in r.stderr.lower()
```

Also create an empty `tools/extract/tests/__init__.py` so `from tests.test_ilbm import make_ilbm` resolves.

- [ ] **Step 2: Run it to verify it fails**

Run: `cd tools/extract && pytest tests/test_cli.py -q`
Expected: 3 failed (no module `extract.__main__`).

- [ ] **Step 3: Write the ADF reader**

```python
# tools/extract/extract/adf.py
"""Read-only access to an AmigaDOS floppy image via amitools."""
from amitools.fs.ADFSVolume import ADFSVolume
from amitools.fs.FSString import FSString
from amitools.fs.blkdev.BlkDevFactory import BlkDevFactory


class AdfError(Exception):
    pass


class Adf:
    def __init__(self, path):
        try:
            self.blkdev = BlkDevFactory().open(str(path), read_only=True)
        except Exception as e:  # amitools raises assorted types
            raise AdfError(f"{path} is not a readable ADF disk image: {e}")
        self.vol = ADFSVolume(self.blkdev)
        try:
            self.vol.open()
        except Exception as e:
            self.blkdev.close()
            raise AdfError(f"{path} does not contain an AmigaDOS filesystem: {e}")

    def read(self, path: str) -> bytes:
        node = self.vol.get_path_name(FSString(path))
        if node is None or not node.is_file():
            raise AdfError(f"file {path} not found on the disk")
        return node.get_file_data()

    def listdir(self, path: str):
        node = self.vol.get_path_name(FSString(path))
        if node is None or node.is_file():
            raise AdfError(f"directory {path} not found on the disk")
        return sorted(e.get_file_name().get_unicode_name() for e in node.get_entries()
                      if e.is_file())

    def close(self):
        self.vol.close()
        self.blkdev.close()
```

- [ ] **Step 4: Write the sound converter**

```python
# tools/extract/extract/sounds.py
"""The disk's text/ directory holds signed 8-bit mono samples, not text."""
import wave

# Provisional. The true playback period comes from RE slice 7 (docs/re/07-sound.md);
# until then the WAV header uses this rate and the manifest flags it as provisional.
SAMPLE_RATE_HZ = 8000
SAMPLE_RATE_PROVISIONAL = True


def write_sample(raw: bytes, raw_path, wav_path, rate_hz: int = SAMPLE_RATE_HZ) -> None:
    with open(raw_path, "wb") as f:
        f.write(raw)
    unsigned = bytes(b ^ 0x80 for b in raw)
    with wave.open(str(wav_path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate_hz)
        w.writeframes(unsigned)
```

- [ ] **Step 5: Write the manifest module**

```python
# tools/extract/extract/manifest.py
import hashlib
import json
from pathlib import Path

EXTRACTOR_VERSION = 1


def sha256_of(path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_manifest(out_dir: Path, source_path, sample_rate_hz: int, provisional: bool) -> None:
    files = {}
    for p in sorted(out_dir.rglob("*")):
        if p.is_file() and p.name != "manifest.json":
            files[p.relative_to(out_dir).as_posix()] = sha256_of(p)
    manifest = {
        "extractor_version": EXTRACTOR_VERSION,
        "source_sha256": sha256_of(source_path),
        "sample_rate_hz": sample_rate_hz,
        "sample_rate_provisional": provisional,
        "files": files,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
```

- [ ] **Step 6: Write the CLI**

```python
# tools/extract/extract/cli.py
"""python -m extract <image.adf> <assets dir>

Copies the game's pictures and sounds from the user's own disk image into a
local assets directory. Nothing produced here may be committed.
"""
import json
import shutil
import sys
import tempfile
from pathlib import Path

from .adf import Adf, AdfError
from .ilbm import decode_ilbm
from .manifest import write_manifest
from .png import write_png
from .sounds import SAMPLE_RATE_HZ, SAMPLE_RATE_PROVISIONAL, write_sample

PICTURES = ["splsh", "inst5", "frnt5"]


def extract(adf_path: Path, out_dir: Path) -> None:
    disk = Adf(adf_path)
    try:
        staging = Path(tempfile.mkdtemp(prefix="interceptor-assets-", dir=out_dir.parent))
        try:
            (staging / "pix").mkdir()
            for name in PICTURES:
                img = decode_ilbm(disk.read(f"pix/{name}"))
                write_png(staging / "pix" / f"{name}.png", img.width, img.height, img.pixels, img.palette)
                (staging / "pix" / f"{name}.palette.json").write_text(
                    json.dumps([list(c) for c in img.palette]) + "\n")
            (staging / "sounds").mkdir()
            for name in disk.listdir("text"):
                raw = disk.read(f"text/{name}")
                write_sample(raw, staging / "sounds" / f"{name}.raw", staging / "sounds" / f"{name}.wav")
            write_manifest(staging, adf_path, SAMPLE_RATE_HZ, SAMPLE_RATE_PROVISIONAL)
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
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    adf_path, out_dir = Path(argv[0]), Path(argv[1])
    if not adf_path.is_file():
        print(f"error: {adf_path} does not exist", file=sys.stderr)
        return 1
    try:
        extract(adf_path, out_dir)
    except (AdfError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"assets written to {out_dir}")
    return 0
```

```python
# tools/extract/extract/__main__.py
import sys
from .cli import main

sys.exit(main())
```

- [ ] **Step 7: Run the CLI tests to verify they pass**

Run: `pytest -q`
Expected: 9 passed. If `test_non_adf_file_is_rejected` fails because amitools opened the bogus file without error, extend `Adf.__init__` to check `self.blkdev.num_blocks` is 1760 and raise `AdfError("... is not an 880 KB ADF")` otherwise; re-run.

- [ ] **Step 8: Run the extractor on the real disk (sandbox only)**

Run: `. /sandbox/.tools/env.sh && cd /sandbox/interceptor-wasm && python -m extract extracted/interceptor.adf assets && ls assets/pix assets/sounds && python -c "import json;print(len(json.load(open('assets/manifest.json'))['files']))"`
Expected: three PNG and three palette files, 32 sound files, manifest with 38 entries. Confirm `git status` shows `assets/` as ignored.

- [ ] **Step 9: Write tools/extract/README.md**

```markdown
# extract

Turns your own *F/A-18 Interceptor* disk image into the `assets/` directory
the port reads. The output is copyrighted data: it is git-ignored and must not
be committed or shared.

    # flux image -> ADF (only if you have an SCP/HFE capture)
    gw convert --format amiga.amigados "disks/scp/FA-18 Interceptor.scp" extracted/interceptor.adf
    # ADF -> assets/
    pip install -e tools/extract
    python -m extract extracted/interceptor.adf assets

Output: `pix/*.png` with `*.palette.json`, `sounds/*.wav` and `*.raw`,
`manifest.json` with hashes. The WAV sample rate is provisional until the
sound RE note fixes it; the `.raw` files are byte-exact.
```

- [ ] **Step 10: Commit**

```bash
git add tools/extract
git commit -m "Extractor CLI: ADF reader, pictures, sounds, manifest

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Hunk relocator producing a flat binary and address map

**Files:**
- Create: `tools/hunk/relocate.py`
- Create: `tools/hunk/tests/test_relocate.py`
- Create: `tools/hunk/README.md`

**Interfaces:**
- Consumes: nothing.
- Produces: `python tools/hunk/relocate.py <hunk file> <out.bin> <out.map.json> [--base 0x10000]` writing a flat little-file (big-endian 68000 bytes) image of all hunks laid out sequentially from `base`, and a JSON map:
  `{"base": 65536, "hunks": [{"index": 0, "type": "CODE", "addr": 65536, "size": 41804}, ...]}` with `type` in `CODE`, `DATA`, `BSS`. Also an importable function `relocate(data: bytes, base: int) -> tuple[bytes, dict]`. Task 5 reads the `.bin` and `.map.json`; Task 6 imports the `.bin` into Ghidra at `base`.

- [ ] **Step 1: Write the failing test with a synthetic hunk file**

```python
# tools/hunk/tests/test_relocate.py
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from relocate import relocate  # noqa: E402

HUNK_HEADER, HUNK_CODE, HUNK_DATA, HUNK_BSS, HUNK_RELOC32, HUNK_END = (
    0x3F3, 0x3E9, 0x3EA, 0x3EB, 0x3EC, 0x3F2)


def L(*words):
    return struct.pack(">%dI" % len(words), *words)


def make_hunk_file():
    """Three hunks laid out CODE (8 bytes), DATA (4 bytes), BSS (16 bytes).
    CODE has one 32-bit reloc at offset 4 pointing into DATA (hunk 1).
    DATA has one 32-bit reloc at offset 0 pointing into BSS (hunk 2), which has no file data."""
    code = bytes.fromhex("4E714E71" "00000000")          # nop nop ; long to patch
    data = bytes.fromhex("00000010")                      # long to patch: 0x10 + BSS base
    header = L(HUNK_HEADER, 0, 3, 0, 2, len(code) // 4, len(data) // 4, 16 // 4)
    code_hunk = L(HUNK_CODE, len(code) // 4) + code
    code_reloc = L(HUNK_RELOC32, 1, 1, 4, 0)              # 1 offset for hunk 1: offset 4; terminator
    data_hunk = L(HUNK_DATA, len(data) // 4) + data
    data_reloc = L(HUNK_RELOC32, 1, 2, 0, 0)              # 1 offset for hunk 2: offset 0; terminator
    bss_hunk = L(HUNK_BSS, 16 // 4)
    return (header + code_hunk + code_reloc + L(HUNK_END)
            + data_hunk + data_reloc + L(HUNK_END)
            + bss_hunk + L(HUNK_END))


def test_relocates_sequential_layout_including_bss_targets():
    image, info = relocate(make_hunk_file(), base=0x1000)
    assert info["base"] == 0x1000
    assert [(h["type"], h["addr"], h["size"]) for h in info["hunks"]] == [
        ("CODE", 0x1000, 8), ("DATA", 0x1008, 4), ("BSS", 0x100C, 16)]
    assert len(image) == 8 + 4 + 16
    assert image[:4] == bytes.fromhex("4E714E71")
    assert struct.unpack(">I", image[4:8])[0] == 0x1008            # CODE+4 patched with DATA base
    assert struct.unpack(">I", image[8:12])[0] == 0x10 + 0x100C    # DATA+0 patched with BSS base
    assert image[12:] == bytes(16)                                 # BSS zero-filled


def test_cli_writes_bin_and_map(tmp_path):
    src = tmp_path / "t.hunk"
    src.write_bytes(make_hunk_file())
    out_bin, out_map = tmp_path / "t.bin", tmp_path / "t.map.json"
    script = Path(__file__).resolve().parents[1] / "relocate.py"
    r = subprocess.run([sys.executable, str(script), str(src), str(out_bin), str(out_map), "--base", "0x1000"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert out_bin.stat().st_size == 28
    m = json.loads(out_map.read_text())
    assert m["hunks"][2]["type"] == "BSS"


def test_rejects_non_hunk_data():
    import pytest
    with pytest.raises(ValueError):
        relocate(b"this is not a hunk file at all", base=0x1000)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd tools/hunk && pytest -q`
Expected: ImportError on `relocate`.

- [ ] **Step 3: Write the relocator**

```python
#!/usr/bin/env python3
"""relocate.py <hunk file> <out.bin> <out.map.json> [--base 0x10000]

Lays every hunk of an AmigaDOS LoadSeg executable out sequentially from a
base address, applies the 32-bit relocations, and writes the flat image plus
a JSON map of hunk addresses. Uses amitools for parsing. The output is a
derivative of copyrighted code and belongs in the git-ignored extracted/.
"""
import argparse
import io
import json
import sys

from amitools.binfmt.hunk.HunkReader import HunkReader
from amitools.binfmt.hunk.HunkRelocate import HunkRelocate

TYPE_NAMES = {"HUNK_CODE": "CODE", "HUNK_DATA": "DATA", "HUNK_BSS": "BSS"}


def relocate(data: bytes, base: int):
    reader = HunkReader()
    result = reader.read_file_obj("hunk", io.BytesIO(data))
    if result != 0 or not reader.build_segments():
        raise ValueError(f"not a valid hunk file: {reader.error_string}")
    rel = HunkRelocate(reader)
    addrs = rel.get_seq_addrs(base)
    datas = rel.relocate(addrs)
    if datas is None:
        raise ValueError("relocation failed")
    sizes = rel.get_sizes()
    types = rel.get_type_names()
    hunks = [{"index": i, "type": TYPE_NAMES.get(types[i], types[i]), "addr": addrs[i], "size": sizes[i]}
             for i in range(len(sizes))]
    image = bytearray()
    for d, size in zip(datas, sizes):
        chunk = bytes(d)
        image += chunk + bytes(size - len(chunk))
    return bytes(image), {"base": base, "hunks": hunks}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("hunk_file")
    ap.add_argument("out_bin")
    ap.add_argument("out_map")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x10000)
    a = ap.parse_args(argv)
    with open(a.hunk_file, "rb") as f:
        data = f.read()
    try:
        image, info = relocate(data, a.base)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    with open(a.out_bin, "wb") as f:
        f.write(image)
    with open(a.out_map, "w") as f:
        json.dump(info, f, indent=2)
    print(f"{len(info['hunks'])} hunks, {len(image)} bytes at 0x{a.base:x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `pytest -q`
Expected: 3 passed. If amitools's `relocate()` returns `ctypes` buffers shorter than `alloc_size` for BSS, the `bytes(size - len(chunk))` padding covers it; if it returns `None` for BSS entries, replace `bytes(d)` with `bytes(d) if d is not None else b""`.

- [ ] **Step 5: Run on the real executable (sandbox only)**

Run: `. /sandbox/.tools/env.sh && cd /sandbox/interceptor-wasm && python tools/hunk/relocate.py "extracted/disk/F-18 Interceptor" extracted/interceptor.bin extracted/interceptor.map.json`
Expected: `185 hunks, 299244 bytes at 0x10000` (0x48fec bytes). Confirm `git status` does not list `extracted/`.

- [ ] **Step 6: Write tools/hunk/README.md** (four lines: purpose, command above, output files, note that output is git-ignored).

- [ ] **Step 7: Commit**

```bash
git add tools/hunk
git commit -m "Hunk relocator: flat image and address map for Ghidra and the rig

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Musashi 68000 test rig

**Files:**
- Create: `tools/m68k-rig/musashi` (git submodule: `https://github.com/kstenerud/Musashi`)
- Create: `tools/m68k-rig/m68kconf.h`
- Create: `tools/m68k-rig/rig.h`
- Create: `tools/m68k-rig/rig.c`
- Create: `tools/m68k-rig/Makefile`
- Create: `tools/m68k-rig/tests/test_rig.c`
- Create: `tools/m68k-rig/README.md`

**Interfaces:**
- Consumes: `extracted/interceptor.bin` and `.map.json` from Task 4 (optional at test time); `tests/test.h` from Task 1.
- Produces:
```c
int      rig_init(size_t mem_size);                      /* allocates RAM, resets CPU; 0 on success */
int      rig_load_image(const char *bin_path, uint32_t base);  /* copies file into RAM at base; 0 ok */
void     rig_poke(uint32_t addr, const uint8_t *src, size_t n);
void     rig_peek(uint32_t addr, uint8_t *dst, size_t n);
uint32_t rig_call(uint32_t addr, const uint32_t *args, int nargs);  /* C ABI: args pushed on stack, returns D0 */
void     rig_shutdown(void);
```
  Later milestones add typed wrappers per RE slice. The map JSON is read by the test scripts in Python, not by the rig, so the rig stays dependency-free.

- [ ] **Step 1: Add the submodule and config**

```bash
cd /sandbox/interceptor-wasm
git submodule add https://github.com/kstenerud/Musashi tools/m68k-rig/musashi
```

`tools/m68k-rig/m68kconf.h`: copy `musashi/m68kconf.h` and change these lines:
```c
#define M68K_EMULATE_ADDRESS_ERROR  M68K_OPT_OFF
#define M68K_INSTRUCTION_HOOK       M68K_OPT_OFF
#define M68K_EMULATE_PREFETCH       M68K_OPT_OFF
#define M68K_SEPARATE_READS         M68K_OPT_OFF
#define M68K_EMULATE_FC             M68K_OPT_OFF
```
(all are already OFF upstream except possibly prefetch; the point is the rig owns its config so upstream changes cannot alter behaviour). Keep the default memory callback names `m68k_read_memory_8/16/32` and `m68k_write_memory_8/16/32`.

- [ ] **Step 2: Write the failing rig test**

```c
/* tools/m68k-rig/tests/test_rig.c */
#include "../../../tests/test.h"
#include "../rig.h"

/* Hand-assembled 68000: move.l 4(sp),d0 ; add.l 8(sp),d0 ; rts */
static const uint8_t add_fn[] = { 0x20, 0x2F, 0x00, 0x04, 0xD0, 0xAF, 0x00, 0x08, 0x4E, 0x75 };

TEST(calls_a_two_argument_function_and_returns_d0) {
    ASSERT_EQ_INT(rig_init(1 << 20), 0);
    rig_poke(0x1000, add_fn, sizeof add_fn);
    uint32_t args[2] = { 3, 4 };
    ASSERT_EQ_INT(rig_call(0x1000, args, 2), 7);
    uint32_t big[2] = { 0xFFFFFFFFu, 2 };
    ASSERT_EQ_INT(rig_call(0x1000, big, 2), 1);   /* 32-bit wraparound */
    rig_shutdown();
}

TEST(peek_reads_back_what_poke_wrote) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    uint8_t in[4] = { 1, 2, 3, 4 }, out[4] = { 0 };
    rig_poke(0x2000, in, 4);
    rig_peek(0x2000, out, 4);
    ASSERT_EQ_INT(memcmp(in, out, 4), 0);
    rig_shutdown();
}

int main(void) {
    RUN(calls_a_two_argument_function_and_returns_d0);
    RUN(peek_reads_back_what_poke_wrote);
    TEST_MAIN_END();
}
```

- [ ] **Step 3: Write the rig Makefile**

```make
# tools/m68k-rig/Makefile
CC     ?= gcc
CFLAGS ?= -O2 -Wall
M      := musashi
GEN    := $(M)/m68kops.c $(M)/m68kops.h

all: test_rig

$(M)/m68kmake: $(M)/m68kmake.c
	$(CC) -o $@ $<

$(GEN): $(M)/m68kmake $(M)/m68k_in.c
	cd $(M) && ./m68kmake

librig.a: rig.c rig.h m68kconf.h $(GEN)
	$(CC) $(CFLAGS) -I. -I$(M) -c rig.c -o rig.o
	$(CC) $(CFLAGS) -I. -I$(M) -c $(M)/m68kcpu.c -o m68kcpu.o
	$(CC) $(CFLAGS) -I. -I$(M) -c $(M)/m68kops.c -o m68kops.o
	$(CC) $(CFLAGS) -I. -I$(M) -c $(M)/m68kdasm.c -o m68kdasm.o
	$(CC) $(CFLAGS) -I. -I$(M) -c $(M)/softfloat/softfloat.c -o softfloat.o
	ar rcs $@ rig.o m68kcpu.o m68kops.o m68kdasm.o softfloat.o

test_rig: tests/test_rig.c librig.a
	$(CC) $(CFLAGS) -I. $< librig.a -o $@ -lm

test: test_rig
	./test_rig

clean:
	rm -f *.o librig.a test_rig $(GEN) $(M)/m68kmake
```

The `-I.` before `-I$(M)` makes Musashi pick up the rig's `m68kconf.h` instead of its own.

- [ ] **Step 4: Run to verify it fails**

Run: `. /sandbox/.tools/env.sh && cd tools/m68k-rig && make test`
Expected: compile error, `rig.h` missing.

- [ ] **Step 5: Write the rig**

```c
/* tools/m68k-rig/rig.h */
#ifndef INTERCEPTOR_RIG_H
#define INTERCEPTOR_RIG_H
#include <stddef.h>
#include <stdint.h>
int      rig_init(size_t mem_size);
int      rig_load_image(const char *bin_path, uint32_t base);
void     rig_poke(uint32_t addr, const uint8_t *src, size_t n);
void     rig_peek(uint32_t addr, uint8_t *dst, size_t n);
uint32_t rig_call(uint32_t addr, const uint32_t *args, int nargs);
void     rig_shutdown(void);
#endif
```

```c
/* tools/m68k-rig/rig.c - Musashi wrapper that calls isolated 68000 functions.
 * Big-endian flat RAM, no chipset, no OS. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
#include "rig.h"

#define RIG_SENTINEL 0x00000100u   /* return address: an ILLEGAL opcode lives here */
#define RIG_STACK_TOP(mem) ((uint32_t)(mem) - 0x100u)

static uint8_t *ram;
static size_t ram_size;

static uint32_t rd(uint32_t a, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | (a + i < ram_size ? ram[a + i] : 0xFF);
    return v;
}
static void wr(uint32_t a, int n, uint32_t v) {
    for (int i = n - 1; i >= 0; i--) { if (a + i < ram_size) ram[a + i] = (uint8_t)(v & 0xFF); v >>= 8; }
}
unsigned int m68k_read_memory_8(unsigned int a)  { return rd(a, 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd(a, 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return rd(a, 4); }
unsigned int m68k_read_disassembler_8(unsigned int a)  { return rd(a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return rd(a, 4); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { wr(a, 1, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { wr(a, 2, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { wr(a, 4, v); }

int rig_init(size_t mem_size) {
    ram = (uint8_t *)calloc(mem_size, 1);
    if (!ram) return -1;
    ram_size = mem_size;
    wr(0, 4, RIG_STACK_TOP(mem_size));   /* reset SP */
    wr(4, 4, RIG_SENTINEL);              /* reset PC */
    wr(RIG_SENTINEL, 2, 0x4AFC);         /* ILLEGAL */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    return 0;
}

int rig_load_image(const char *bin_path, uint32_t base) {
    FILE *f = fopen(bin_path, "rb");
    if (!f) return -1;
    size_t n = fread(ram + base, 1, ram_size - base, f);
    fclose(f);
    return n > 0 ? 0 : -1;
}

void rig_poke(uint32_t addr, const uint8_t *src, size_t n) { memcpy(ram + addr, src, n); }
void rig_peek(uint32_t addr, uint8_t *dst, size_t n)       { memcpy(dst, ram + addr, n); }

uint32_t rig_call(uint32_t addr, const uint32_t *args, int nargs) {
    uint32_t sp = RIG_STACK_TOP(ram_size);
    for (int i = nargs - 1; i >= 0; i--) { sp -= 4; wr(sp, 4, args[i]); }
    sp -= 4; wr(sp, 4, RIG_SENTINEL);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, addr);
    for (int guard = 0; guard < 1000000; guard++) {
        m68k_execute(64);
        if (m68k_get_reg(NULL, M68K_REG_PC) == RIG_SENTINEL) break;
    }
    return m68k_get_reg(NULL, M68K_REG_D0);
}

void rig_shutdown(void) { free(ram); ram = NULL; ram_size = 0; }
```

- [ ] **Step 6: Run the rig test to verify it passes**

Run: `make test`
Expected: `2 tests, 0 failures`. If the loop never sees `PC == RIG_SENTINEL` because Musashi stops at the ILLEGAL instruction with PC already advanced, change the check to `m68k_get_reg(NULL, M68K_REG_PC) >= RIG_SENTINEL && ... < RIG_SENTINEL + 4` and set `M68K_INSTRUCTION_HOOK` to `M68K_OPT_ON` with a hook that calls `m68k_end_timeslice()` when `pc == RIG_SENTINEL`.

- [ ] **Step 7: Smoke-load the real image (sandbox only)**

Add to `tests/test_rig.c`:
```c
TEST(loads_relocated_game_image_when_present) {
    FILE *f = fopen("../../extracted/interceptor.bin", "rb");
    if (!f) { printf("(skipped, no extracted/) "); return; }
    fclose(f);
    ASSERT_EQ_INT(rig_init(1 << 21), 0);
    ASSERT_EQ_INT(rig_load_image("../../extracted/interceptor.bin", 0x10000), 0);
    uint8_t first[2];
    rig_peek(0x10000, first, 2);
    ASSERT_TRUE(first[0] != 0 || first[1] != 0);
    rig_shutdown();
}
```
and `RUN` it. Run `make test`: 3 tests pass in the sandbox; the test prints "skipped" in CI.

- [ ] **Step 8: Write tools/m68k-rig/README.md** (purpose: differential tests against original pure functions; build: `make test`; the submodule; note that the game image comes from Task 4's relocator and lives in git-ignored `extracted/`).

- [ ] **Step 9: Commit**

```bash
git add .gitmodules tools/m68k-rig
git commit -m "Musashi rig: call isolated 68000 functions for differential tests

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Ghidra import and binary layout note

**Files:**
- Create: `tools/hunk/ghidra_import.md`
- Create: `tools/hunk/layout_table.py`
- Create: `docs/re/00-binary-layout.md`

**Interfaces:**
- Consumes: `extracted/interceptor.bin` and `extracted/interceptor.map.json` from Task 4.
- Produces: a Ghidra project that every RE slice opens, and the layout note every RE note references for addresses.

- [ ] **Step 1: Write the layout table generator**

```python
#!/usr/bin/env python3
"""layout_table.py <map.json> -> Markdown table of hunks (index, type, address, size)."""
import json
import sys

m = json.load(open(sys.argv[1]))
print("| hunk | type | address | size |")
print("|-----:|------|--------:|-----:|")
for h in m["hunks"]:
    print(f"| {h['index']} | {h['type']} | 0x{h['addr']:06X} | {h['size']} |")
```

Run: `python tools/hunk/layout_table.py extracted/interceptor.map.json | head -5` and confirm a table appears.

- [ ] **Step 2: Import into Ghidra through rea**

Use the rea MCP tool `open_binary` on `extracted/interceptor.bin` with `REA_GHIDRA_LANGUAGE_ID=68000:BE:32:default` and base address `0x10000` (rea's `open_binary` accepts a language and base; if base cannot be passed, open it and record the offset in the note). Then run `binary_overview` and `list_procedures`. Expected: dozens of functions detected in hunk 0 starting at 0x10000; the `jsr`-based OS library stubs (`_OpenLibrary` and friends found as strings near file offset 328000) decompile as short jump-table thunks.

Write `tools/hunk/ghidra_import.md` recording the exact rea calls and settings used, so any agent can reopen the same project.

- [ ] **Step 3: Write the layout note**

`docs/re/00-binary-layout.md` contains, in prose and tables only:
- how the image was produced (relocator command, base address);
- the hunk table from Step 1;
- which hunk ranges are C-compiled code with OS stubs, which are hand-written assembly, and which are data (by inspection of instruction style: `link`/`unlk` frames versus register-heavy loops), each as an address range and one sentence;
- where the string tables live (menus, briefings, warnings) by address;
- the list of Amiga libraries and devices the binary opens, from the strings already observed: exec, dos, graphics, intuition, audio.device, gameport.device, keyboard.device, timer.device, input.device;
- an "open questions" list.

No disassembly, no decompiler output, no code.

- [ ] **Step 4: Commit**

```bash
git add tools/hunk/ghidra_import.md tools/hunk/layout_table.py docs/re/00-binary-layout.md
git commit -m "Ghidra import procedure and binary layout note

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Headless reference emulator with the AROS ROM

**Files:**
- Create: `tools/vamiga-ref/fetch_aros_rom.py`
- Create: `tools/vamiga-ref/build.sh`
- Create: `tools/vamiga-ref/ref.cpp`
- Create: `tools/vamiga-ref/make_autoboot_adf.sh`
- Create: `tools/vamiga-ref/README.md`
- Create: `docs/re/01-reference-emulator.md`

**Interfaces:**
- Consumes: `extracted/interceptor.adf` (from Greaseweazle, already present).
- Produces: `build/vamiga-ref/ref` with usage `ref --rom <aros.rom> --ext <aros.ext> --adf <disk.adf> --seconds <n> --out <frame.ppm>`; writes the emulator's stable framebuffer as a binary PPM (P6). Also `extracted/aros.rom` and `extracted/aros.ext`, and `extracted/interceptor-autoboot.adf` whose startup-sequence launches the game directly.

- [ ] **Step 1: Write and run the ROM fetcher**

```python
#!/usr/bin/env python3
"""Fetch the AROS Kickstart replacement (open source, APL) embedded in FS-UAE's
source as a gzip blob, and split it into rom + ext halves for vAmiga."""
import gzip
import re
import sys
import urllib.request
from pathlib import Path

URL = "https://raw.githubusercontent.com/FrodeSolheim/fs-uae/master/aros.rom.cpp"
out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "extracted")
src = urllib.request.urlopen(URL).read().decode()
body = src[src.index("{") + 1:src.rindex("}")]
blob = bytes(int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{2}", body))
rom = gzip.decompress(blob)
assert len(rom) == 1024 * 1024, len(rom)
(out_dir / "aros.rom").write_bytes(rom[:512 * 1024])
(out_dir / "aros.ext").write_bytes(rom[512 * 1024:])
print("wrote", out_dir / "aros.rom", out_dir / "aros.ext")
```

Run: `python tools/vamiga-ref/fetch_aros_rom.py extracted` → two 512 KB files. Both start with bytes `11 14 4e f9`.

- [ ] **Step 2: Write the build script**

```bash
#!/usr/bin/env bash
# tools/vamiga-ref/build.sh - build vAmiga Core headless plus our driver. Linux sandbox only.
set -euo pipefail
. /sandbox/.tools/env.sh
micromamba install -y -p /sandbox/.tools/env -c conda-forge cmake zlib >/dev/null
SRC=/sandbox/vamiga
[ -d "$SRC" ] || git clone --depth 1 https://github.com/dirkwhoffmann/vAmiga "$SRC"
BUILD=/sandbox/interceptor-wasm/build/vamiga-ref
cmake -S "$SRC/Core" -B "$BUILD/core" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD/core" -j"$(nproc)" --target VACore
g++ -std=c++20 -O2 -I"$SRC/Core" -I"$SRC/Core/Infrastructure" -I"$SRC/Core/Components" \
    tools/vamiga-ref/ref.cpp "$BUILD/core/libVACore.a" -lz -lpthread -o "$BUILD/ref"
echo "built $BUILD/ref"
```

- [ ] **Step 3: Write the driver**

```cpp
// tools/vamiga-ref/ref.cpp - boot a disk in vAmiga headless and dump a frame.
#include "VAmiga.h"
#include "Constants.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

using namespace vamiga;

static void on_message(const void *, Message) {}

int main(int argc, char **argv) {
    std::string rom, ext, adf, out = "frame.ppm";
    int seconds = 30;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--rom") rom = v; else if (k == "--ext") ext = v; else if (k == "--adf") adf = v;
        else if (k == "--seconds") seconds = std::stoi(v); else if (k == "--out") out = v;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    if (rom.empty()) { std::fprintf(stderr, "usage: ref --rom R --ext E --adf D --seconds N --out F\n"); return 2; }

    VAmiga va;
    va.mem.loadRom(std::filesystem::path(rom));
    if (!ext.empty()) va.mem.loadExt(std::filesystem::path(ext));
    if (!adf.empty()) va.df0.insert(std::filesystem::path(adf), true);
    va.launch(nullptr, on_message);
    va.run();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    va.pause();

    va.videoPort.lockTexture();
    const u32 *tex = va.videoPort.getTexture();
    const int w = (int)HPIXELS, h = (int)VPIXELS;
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int i = 0; i < w * h; i++) {
        u32 p = tex[i];
        unsigned char rgb[3] = { (unsigned char)(p & 0xFF), (unsigned char)((p >> 8) & 0xFF),
                                 (unsigned char)((p >> 16) & 0xFF) };
        f.write((const char *)rgb, 3);
    }
    std::printf("wrote %s (%dx%d)\n", out.c_str(), w, h);
    return 0;
}
```

Build with `bash tools/vamiga-ref/build.sh`. If the texture constants are named differently, read `Core/Components/Denise/Texture.h` and use `Texture::width` and `Texture::height`; if the pixel byte order looks wrong in the output, swap the R and B expressions. If `VAmiga` needs a machine configuration before `launch`, call `va.set(ConfigScheme::A500_OCS_1MB)` with the enum name found in `Core/Components/Amiga/AmigaTypes.h` or `Core/Infrastructure/EmulatorTypes.h`.

- [ ] **Step 4: Smoke test without a disk**

Run: `build/vamiga-ref/ref --rom extracted/aros.rom --ext extracted/aros.ext --seconds 15 --out /tmp/aros-boot.ppm && python3 -c "
d=open('/tmp/aros-boot.ppm','rb').read(); hdr_end=d.index(b'255\n')+4; px=d[hdr_end:]
print('distinct colours:', len(set(px[i:i+3] for i in range(0,len(px),3))))"`
Expected: more than 2 distinct colours (the AROS boot screen, not a black frame). Convert to PNG for viewing with `micromamba install -c conda-forge imagemagick` then `magick /tmp/aros-boot.ppm /tmp/aros-boot.png`, and look at it with the Read tool.

- [ ] **Step 5: Make the autoboot disk and boot the game**

```bash
#!/usr/bin/env bash
# tools/vamiga-ref/make_autoboot_adf.sh - copy of the game disk whose startup-sequence runs the game.
set -euo pipefail
cp extracted/interceptor.adf extracted/interceptor-autoboot.adf
printf 'run "F-18 Interceptor"\n' > /tmp/startup-sequence
xdftool extracted/interceptor-autoboot.adf delete s/startup-sequence + write /tmp/startup-sequence s/startup-sequence
```

Run it, then: `build/vamiga-ref/ref --rom extracted/aros.rom --ext extracted/aros.ext --adf extracted/interceptor-autoboot.adf --seconds 60 --out /tmp/game-boot.ppm`, convert to PNG, view it. Repeat with `--seconds 120` if the first frame still shows the AROS boot.

- [ ] **Step 6: Record the outcome**

`docs/re/01-reference-emulator.md`: how to build and run the reference emulator, the exact commands, what the boot looked like under AROS at 60 and 120 seconds (one sentence each, and the PNGs are not committed), and the verdict: either "AROS boots the game; use it for reference captures" or "AROS fails at <symptom>; a Kickstart 1.2 ROM is required, place it at `disks/kick12.rom`". Report this verdict to the user in the task summary, since it decides a purchase.

- [ ] **Step 7: Write tools/vamiga-ref/README.md** (purpose, build, the three commands above, that all outputs are git-ignored under `extracted/` and `build/`).

- [ ] **Step 8: Commit**

```bash
git add tools/vamiga-ref docs/re/01-reference-emulator.md
git commit -m "Headless vAmiga reference emulator with the AROS ROM

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: SDL frontend skeleton showing the splash

**Files:**
- Create: `src/frontend/sdl/stb_image.h` (vendored from `https://raw.githubusercontent.com/nothings/stb/master/stb_image.h`, public domain / MIT, keep its header comment)
- Create: `src/frontend/sdl/assets.h`, `src/frontend/sdl/assets.c`
- Create: `src/frontend/sdl/video.h`, `src/frontend/sdl/video.c`
- Create: `src/frontend/sdl/main.c`
- Create: `tests/frontend/test_splash.py`

**Interfaces:**
- Consumes: `game.h`, `scene.h` from Task 1; `assets/manifest.json` and `assets/pix/splsh.png` layout from Task 3.
- Produces: `build/interceptor [--assets DIR] [--frames N] [--dump FILE.ppm] [--scale K]`. Exit 1 with a message naming `python -m extract` when the assets directory or manifest is missing. With `--frames N --dump F` it renders N frames and writes the last as PPM, which works under `SDL_VIDEODRIVER=dummy`.
  - `assets.h`: `int assets_open(const char *dir, game_loader_t *out, char *err, size_t errlen);` (validates the manifest exists, fills a loader whose `read` loads `dir/path`), `void assets_close(game_loader_t *);`
  - `video.h`: `typedef struct video video_t; video_t *video_create(int scale, char *err, size_t errlen); void video_render(video_t *, const game_scene_t *, const game_loader_t *); int video_dump_ppm(video_t *, const char *path); void video_destroy(video_t *);`

- [ ] **Step 1: Write the failing frontend test**

```python
# tests/frontend/test_splash.py - runs the built frontend headless against a synthetic assets dir.
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
sys.path.insert(0, str(ROOT / "tools" / "extract"))
from extract.png import write_png  # noqa: E402

pytestmark = pytest.mark.skipif(not BIN.exists(), reason="build/interceptor not built")


def make_assets(tmp_path):
    (tmp_path / "pix").mkdir()
    pixels = bytes([1] * 320 * 200)                       # solid colour index 1
    write_png(tmp_path / "pix" / "splsh.png", 320, 200, pixels, [(0, 0, 0), (255, 0, 0)])
    (tmp_path / "manifest.json").write_text(json.dumps({"extractor_version": 1, "files": {}}))
    return tmp_path


def run(args, env_extra=None):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", **(env_extra or {}))
    return subprocess.run([str(BIN), *args], capture_output=True, text=True, env=env)


def test_missing_assets_dir_exits_with_extractor_hint(tmp_path):
    r = run(["--assets", str(tmp_path / "nope"), "--frames", "1"])
    assert r.returncode == 1
    assert "python -m extract" in r.stderr


def test_splash_frame_is_the_picture(tmp_path):
    assets = make_assets(tmp_path)
    out = tmp_path / "frame.ppm"
    r = run(["--assets", str(assets), "--frames", "2", "--dump", str(out), "--scale", "1"])
    assert r.returncode == 0, r.stderr
    data = out.read_bytes()
    assert data.startswith(b"P6\n320 200\n255\n")
    px = data[len(b"P6\n320 200\n255\n"):]
    assert px[:3] == bytes([255, 0, 0]) and px[-3:] == bytes([255, 0, 0])
```

- [ ] **Step 2: Run it to verify it fails**

Run: `. /sandbox/.tools/env.sh && micromamba install -y -p /sandbox/.tools/env -c conda-forge sdl2 pytest && make && pytest tests/frontend -q`
Expected: `make` fails (no frontend sources) so the tests are skipped; that is the failing state.

- [ ] **Step 3: Write assets.c**

```c
/* src/frontend/sdl/assets.h */
#ifndef INTERCEPTOR_SDL_ASSETS_H
#define INTERCEPTOR_SDL_ASSETS_H
#include <stddef.h>
#include "../../core/game.h"
int  assets_open(const char *dir, game_loader_t *out, char *err, size_t errlen);
void assets_close(game_loader_t *loader);
#endif
```

```c
/* src/frontend/sdl/assets.c - file-backed asset loader for the core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"

typedef struct { char dir[1024]; } assets_ctx_t;

static int read_file(void *user, const char *path, uint8_t **out, size_t *len) {
    assets_ctx_t *ctx = (assets_ctx_t *)user;
    char full[2048];
    snprintf(full, sizeof full, "%s/%s", ctx->dir, path);
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return -1; }
    *out = buf; *len = got;
    return 0;
}

static void release_file(void *user, uint8_t *buf) { (void)user; free(buf); }

int assets_open(const char *dir, game_loader_t *out, char *err, size_t errlen) {
    char full[2048];
    snprintf(full, sizeof full, "%s/manifest.json", dir);
    FILE *f = fopen(full, "rb");
    if (!f) {
        snprintf(err, errlen, "no assets at %s. Run: python -m extract <your.adf> %s", dir, dir);
        return -1;
    }
    fclose(f);
    assets_ctx_t *ctx = (assets_ctx_t *)calloc(1, sizeof *ctx);
    strncpy(ctx->dir, dir, sizeof ctx->dir - 1);
    out->read = read_file; out->release = release_file; out->user = ctx;
    return 0;
}

void assets_close(game_loader_t *loader) { free(loader->user); loader->user = NULL; }
```

- [ ] **Step 4: Write video.c**

```c
/* src/frontend/sdl/video.h */
#ifndef INTERCEPTOR_SDL_VIDEO_H
#define INTERCEPTOR_SDL_VIDEO_H
#include <stddef.h>
#include "../../core/game.h"
typedef struct video video_t;
video_t *video_create(int scale, char *err, size_t errlen);
void     video_render(video_t *v, const game_scene_t *scene, const game_loader_t *loader);
int      video_dump_ppm(video_t *v, const char *path);
void     video_destroy(video_t *v);
#endif
```

```c
/* src/frontend/sdl/video.c - 320x200 framebuffer, integer-scaled to the window.
 * Milestone 1: draws the splash picture only. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include "video.h"

#define FB_W 320
#define FB_H 200

struct video {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t fb[FB_W * FB_H];           /* 0xRRGGBB */
    uint8_t *splash_rgb;                /* decoded picture, 3 bytes per pixel, or NULL */
    int splash_w, splash_h;
    uint16_t splash_loaded_id;
    int splash_loaded;
};

video_t *video_create(int scale, char *err, size_t errlen) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { snprintf(err, errlen, "SDL_Init: %s", SDL_GetError()); return NULL; }
    video_t *v = (video_t *)calloc(1, sizeof *v);
    v->win = SDL_CreateWindow("F/A-18 Interceptor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              FB_W * scale, FB_H * scale, 0);
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    v->tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!v->win || !v->ren || !v->tex) { snprintf(err, errlen, "SDL: %s", SDL_GetError()); video_destroy(v); return NULL; }
    return v;
}

static void load_splash(video_t *v, uint16_t id, const game_loader_t *loader) {
    static const char *names[] = { "pix/splsh.png", "pix/inst5.png", "pix/frnt5.png" };
    if (v->splash_loaded && v->splash_loaded_id == id) return;
    free(v->splash_rgb); v->splash_rgb = NULL;
    uint8_t *png; size_t len;
    if (id >= 3 || loader->read(loader->user, names[id], &png, &len) != 0) return;
    int ch;
    v->splash_rgb = stbi_load_from_memory(png, (int)len, &v->splash_w, &v->splash_h, &ch, 3);
    loader->release(loader->user, png);
    v->splash_loaded = 1; v->splash_loaded_id = id;
}

void video_render(video_t *v, const game_scene_t *scene, const game_loader_t *loader) {
    memset(v->fb, 0, sizeof v->fb);
    if (scene->mode == GAME_MODE_SPLASH) {
        load_splash(v, scene->splash_asset, loader);
        if (v->splash_rgb) {
            int w = v->splash_w < FB_W ? v->splash_w : FB_W, h = v->splash_h < FB_H ? v->splash_h : FB_H;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const uint8_t *p = v->splash_rgb + (y * v->splash_w + x) * 3;
                    v->fb[y * FB_W + x] = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
                }
        }
    }
    SDL_UpdateTexture(v->tex, NULL, v->fb, FB_W * 4);
    SDL_RenderClear(v->ren);
    SDL_RenderCopy(v->ren, v->tex, NULL, NULL);
    SDL_RenderPresent(v->ren);
}

int video_dump_ppm(video_t *v, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint8_t rgb[3] = { (uint8_t)(v->fb[i] >> 16), (uint8_t)(v->fb[i] >> 8), (uint8_t)v->fb[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

void video_destroy(video_t *v) {
    if (!v) return;
    free(v->splash_rgb);
    if (v->tex) SDL_DestroyTexture(v->tex);
    if (v->ren) SDL_DestroyRenderer(v->ren);
    if (v->win) SDL_DestroyWindow(v->win);
    free(v);
    SDL_Quit();
}
```

Note for the Emscripten build later: `stbi_load_from_memory` and SDL's software renderer both work under emcc unchanged.

- [ ] **Step 5: Write main.c**

```c
/* src/frontend/sdl/main.c - SDL2 frontend entry point. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/game.h"
#include "assets.h"
#include "video.h"

#define TICK_MS 20   /* placeholder pacing for the splash only; the real tick rate comes from docs/re */

int main(int argc, char **argv) {
    const char *assets_dir = "assets", *dump = NULL;
    int frames = -1, scale = 3;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets_dir = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else { fprintf(stderr, "usage: interceptor [--assets DIR] [--frames N] [--dump FILE.ppm] [--scale K]\n"); return 2; }
    }
    char err[256] = "";
    game_loader_t loader;
    if (assets_open(assets_dir, &loader, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 1; }
    game_t *g = game_create(&loader, err, sizeof err);
    if (!g) { fprintf(stderr, "%s\n", err); assets_close(&loader); return 1; }
    video_t *v = video_create(scale, err, sizeof err);
    if (!v) { fprintf(stderr, "%s\n", err); game_destroy(g); assets_close(&loader); return 1; }

    game_scene_t scene;
    int running = 1;
    for (int n = 0; running && (frames < 0 || n < frames); n++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                game_event_t ev = { GAME_EV_BUTTON, GAME_BTN_ANY, 1 };
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0; else game_input(g, &ev);
            }
        }
        game_step(g);
        game_scene(g, &scene);
        video_render(v, &scene, &loader);
        if (frames < 0) SDL_Delay(TICK_MS);
    }
    int rc = 0;
    if (dump && video_dump_ppm(v, dump) != 0) { fprintf(stderr, "could not write %s\n", dump); rc = 1; }
    video_destroy(v);
    game_destroy(g);
    assets_close(&loader);
    return rc;
}
```

The `TICK_MS` constant paces only the splash loop and is replaced in milestone 3 by the tick rate from the RE notes; it is not a game constant.

- [ ] **Step 6: Build and run the tests to verify they pass**

Run: `make && pytest tests/frontend -q`
Expected: 2 passed. Then with the real assets: `SDL_VIDEODRIVER=dummy build/interceptor --frames 1 --dump /tmp/splash.ppm && magick /tmp/splash.ppm /tmp/splash.png` and view the PNG with the Read tool: it must show the game's splash picture.

- [ ] **Step 7: Commit**

```bash
git add src/frontend/sdl tests/frontend
git commit -m "SDL frontend skeleton: assets loader, 320x200 framebuffer, splash, headless dump

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: Toolchain script, tcc build and Emscripten build

**Files:**
- Create: `tools/setup-toolchain.sh`
- Create: `src/frontend/web/shell.html`
- Modify: `Makefile` (web target gains `--shell-file`)
- Modify: `README.md` (toolchain section)

**Interfaces:**
- Consumes: Makefile from Task 1, frontend from Task 8.
- Produces: `tools/setup-toolchain.sh` (idempotent: installs sdl2, cmake, zlib, emscripten, pytest via micromamba; builds tcc 0.9.27 from the TinyCC GitHub mirror into `/sandbox/.tools/tcc`), `make CC=tcc core test` passing, `make web` producing `build/web/interceptor.html`, `.js`, `.wasm`, `.data`.

- [ ] **Step 1: Write the toolchain script**

```bash
#!/usr/bin/env bash
# tools/setup-toolchain.sh - everything the sandbox needs beyond openshell/setup-rea.sh. Safe to re-run.
set -euo pipefail
. /sandbox/.tools/env.sh
micromamba install -y -p /sandbox/.tools/env -c conda-forge gcc gxx make pip sdl2 cmake zlib emscripten pytest
/sandbox/.tools/env/bin/pip install -q "git+https://github.com/keirf/greaseweazle@latest" amitools
/sandbox/.tools/env/bin/pip install -q -e /sandbox/interceptor-wasm/tools/extract
if [ ! -x /sandbox/.tools/tcc/bin/tcc ]; then
  rm -rf /tmp/tinycc && git clone -q --depth 1 --branch release_0_9_27 https://github.com/TinyCC/tinycc /tmp/tinycc
  (cd /tmp/tinycc && ./configure --prefix=/sandbox/.tools/tcc >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)
fi
grep -q '/sandbox/.tools/tcc/bin' /sandbox/.tools/env.sh || \
  sed -i 's|^export PATH="|export PATH="/sandbox/.tools/tcc/bin:|' /sandbox/.tools/env.sh
echo "toolchain ready: $(gcc --version | head -1); $(/sandbox/.tools/tcc/bin/tcc -v); $(emcc --version | head -1)"
```

Run it. Expected: the final line prints all three versions.

- [ ] **Step 2: Verify the tcc build**

Run: `. /sandbox/.tools/env.sh && make clean && make CC=tcc CFLAGS="-std=c99 -Wall" core test`
Expected: `3 tests, 0 failures`. If tcc rejects a construct, fix the core source (never add compiler-specific code).

Run: `make CC=tcc frontend && SDL_VIDEODRIVER=dummy build/interceptor --frames 1 --dump /tmp/t.ppm`
Expected: builds and writes the PPM. If tcc cannot link SDL2 from conda-forge, add `-L/sandbox/.tools/env/lib -Wl,-rpath,/sandbox/.tools/env/lib` to `SDL_LIBS` via `LDFLAGS` in the Makefile and record the command in README.

- [ ] **Step 3: Add the web shell and build**

`src/frontend/web/shell.html`:
```html
<!doctype html>
<html><head><meta charset="utf-8"><title>F/A-18 Interceptor</title>
<style>body{margin:0;background:#000;display:flex;justify-content:center;align-items:center;height:100vh}canvas{image-rendering:pixelated}</style>
</head><body>
<canvas id="canvas" tabindex="1" oncontextmenu="event.preventDefault()"></canvas>
<script>var Module={canvas:document.getElementById('canvas'),arguments:['--assets','/assets']};</script>
{{{ SCRIPT }}}
</body></html>
```

In the Makefile's web rule add `--shell-file src/frontend/web/shell.html` and `-sASYNCIFY` (so the `SDL_Delay` loop yields to the browser in milestone 1; milestone 6 replaces it with `emscripten_set_main_loop`).

Run: `make web`
Expected: `build/web/interceptor.html`, `.js`, `.wasm`, `.data` exist. `--preload-file assets@/assets` requires `assets/` to exist; document that `make web` needs extracted assets.

- [ ] **Step 4: Serve and check in a browser-like check**

Run: `cd build/web && python3 -m http.server 8080 &` then `curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/interceptor.html` → 200, and `ls -la interceptor.wasm` shows a non-trivial size. Kill the server. (A visual check happens on the user's machine; the sandbox has no browser.)

- [ ] **Step 5: Update README.md** with a "Toolchain (sandbox)" section: `bash tools/setup-toolchain.sh`, then the three build commands, and "open `build/web/interceptor.html` through any static file server".

- [ ] **Step 6: Commit**

```bash
git add tools/setup-toolchain.sh src/frontend/web Makefile README.md
git commit -m "Toolchain script, tcc iteration build and Emscripten web build

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 10: Continuous integration on Linux, Windows and macOS arm64

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `make core test`, `make frontend`, `pytest` suites from Tasks 1, 2, 3, 4, 8.
- Produces: a green check on every push proving the core, tests and SDL frontend build on all three platforms.

- [ ] **Step 1: Write the workflow**

```yaml
name: ci
on: [push, pull_request]
jobs:
  linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: true }
      - run: sudo apt-get update && sudo apt-get install -y libsdl2-dev tcc
      - uses: actions/setup-python@v5
        with: { python-version: "3.12" }
      - run: pip install pytest amitools -e tools/extract
      - run: make core test
      - run: make clean && make CC=tcc CFLAGS="-std=c99 -Wall" core test
      - run: make clean && make
      - run: pytest tools/extract tools/hunk tests/frontend -q
      - run: make -C tools/m68k-rig test
  windows:
    runs-on: windows-latest
    defaults: { run: { shell: msys2 {0} } }
    steps:
      - uses: actions/checkout@v4
        with: { submodules: true }
      - uses: msys2/setup-msys2@v2
        with:
          msystem: MINGW64
          install: mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkg-config make
      - run: make core test
      - run: make
      - run: make -C tools/m68k-rig test
  macos:
    runs-on: macos-latest   # Apple Silicon runners
    steps:
      - uses: actions/checkout@v4
        with: { submodules: true }
      - run: brew install sdl2 pkg-config
      - run: make CC=clang core test
      - run: make CC=clang
      - run: make -C tools/m68k-rig CC=clang test
```

- [ ] **Step 2: Verify locally what can be verified**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml')); print('yaml ok')"` (install `pyyaml` with pip if missing). Run every Linux step locally in the sandbox once; all must pass.

- [ ] **Step 3: Commit and push, then watch the run**

```bash
git add .github/workflows/ci.yml
git commit -m "CI: build core, tests and SDL frontend on Linux, Windows and macOS arm64

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
git push origin main
gh run watch --exit-status || gh run view --log-failed
```

Fix any platform-specific failure in the Makefile or sources (common ones: `ar` missing on msys2 → add `binutils` to the install list; `-lm` ordering on macOS is harmless). Re-push until all three jobs are green. Record the run URL in the task summary.

---

### Task 11: Wrap-up: documentation and verification

**Files:**
- Modify: `README.md`
- Modify: `CLAUDE.md` (add a "Daily commands" section)
- Create: `docs/re/README.md`

- [ ] **Step 1: Write docs/re/README.md**

Explain the clean-room rule for notes (prose and tables, no listings, no code, no decompiler output), the slice list from the spec with their intended file names `02-assets-and-tables.md` through `08-sound.md`, and the required sections of every note: Location, Format or Behaviour, Constants, Open questions, Verified by (which rig test or emulator capture).

- [ ] **Step 2: Add "Daily commands" to CLAUDE.md**

```markdown
## Daily commands

    bash tools/setup-toolchain.sh                      # once per sandbox
    python -m extract extracted/interceptor.adf assets # assets (git-ignored)
    python tools/hunk/relocate.py "extracted/disk/F-18 Interceptor" extracted/interceptor.bin extracted/interceptor.map.json
    make CC=tcc core test                              # fast iteration
    make && SDL_VIDEODRIVER=dummy build/interceptor --frames 1 --dump /tmp/f.ppm
    make -C tools/m68k-rig test
    bash tools/vamiga-ref/build.sh                     # reference emulator
```

- [ ] **Step 3: Full verification pass**

Run, in order, and paste the output lines into the commit message body:
```
make clean && make core test
make CC=tcc core test
make && pytest tools/extract tools/hunk tests/frontend -q
make -C tools/m68k-rig test
git status --short            # must not list assets/, extracted/, disks/ or *.rom
```

- [ ] **Step 4: Commit and push**

```bash
git add README.md CLAUDE.md docs/re/README.md
git commit -m "Milestone 1 wrap-up: RE note conventions and daily commands

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
git push origin main
```

# Makefile - one build for core, tests, SDL frontend and web.
CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
BUILD   ?= build

CORE_SRC := $(wildcard src/core/*.c)
# Core objects live in one directory per compiler, so that "make CC=tcc core test" followed by "make test" never links tcc objects with gcc
# (the "overlapping FDEs" failure). The binaries keep their paths (build/interceptor, build/tests/*, build/libcore.a); build/cc.stamp holds the
# compiler and flags of the last build and is rewritten (only) when they change, which relinks every binary.
OBJ      := $(BUILD)/obj-$(notdir $(firstword $(CC)))
CORE_OBJ := $(patsubst src/core/%.c,$(OBJ)/core/%.o,$(CORE_SRC))
STAMP    := $(BUILD)/cc.stamp
FLAGSTAMP := $(OBJ)/flags.stamp
TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(patsubst tests/%.c,$(BUILD)/tests/%,$(TEST_SRC))

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)
SDL_RPATH  := -Wl,-rpath,$(shell pkg-config --variable=libdir sdl2 2>/dev/null)
LDFLAGS    ?= $(SDL_RPATH)
FRONTEND_SRC := $(wildcard src/frontend/sdl/*.c)

.PHONY: all core test world_dump golden golden-update-real golden-update-synthetic frontend web clean

all: core frontend

core: $(BUILD)/libcore.a

.PHONY: FORCE
FORCE:
$(STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(CC) $(CFLAGS)' | cmp -s - $@ || echo '$(CC) $(CFLAGS)' > $@
$(FLAGSTAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(CC) $(CFLAGS)' | cmp -s - $@ || echo '$(CC) $(CFLAGS)' > $@

$(BUILD)/libcore.a: $(CORE_OBJ) $(STAMP)
	rm -f $@
	$(AR) rcs $@ $(CORE_OBJ)

$(OBJ)/core/%.o: $(FLAGSTAMP) src/core/%.c src/core/game.h src/core/scene.h src/core/fx.h src/core/world.h src/core/aircraft.h src/core/assets.h src/core/cockpit.h src/core/flight.h src/core/flight_int.h src/core/lists.h src/core/sky.h src/core/screen.h src/core/replay.h src/core/logbook.h src/core/training.h src/core/entities.h src/core/ai.h src/core/weapons.h src/core/radar.h src/core/missions.h src/core/mapmarks.h src/core/sound.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $(word 2,$^) -o $@

$(BUILD)/tests/%: tests/%.c $(BUILD)/libcore.a $(STAMP) tests/test.h tests/world_synth.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(BUILD)/libcore.a -o $@ -lm

# the chain tests of test_sound.c include the mixer's source
$(BUILD)/tests/test_sound: src/frontend/sdl/audio.c src/frontend/sdl/audio.h

$(BUILD)/tests/world_dump: tests/world_dump.c $(BUILD)/libcore.a $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(BUILD)/libcore.a -o $@ -lm

world_dump: $(BUILD)/tests/world_dump

# golden scenario runs (tests/golden/run_golden.c): integer-only output, identical on every platform. The synthetic run always
# executes (no flight tables except in the mission scripts, which say synthflight; it covers the input, view and scene plumbing); the real-assets run only when assets/models/index.txt exists.
$(BUILD)/tests/run_golden: tests/golden/run_golden.c tests/world_synth.h $(BUILD)/libcore.a $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(BUILD)/libcore.a -o $@ -lm

# The scripts of tests/golden: NAME.txt gives NAME-synthetic.golden (committed) and NAME-real.golden (local, git-ignored). The synthetic run has no flight
# tables beyond a placeholder (the mission scripts add the invented ones: synthflight): it asserts the screens, modes and text plumbing and the six missions to an end code; the real run carries the flights.
GOLDENS := takeoff-and-circuit demo training qualification mission-f1 mission-f2 mission-f3 mission-f4 mission-f5 mission-f6
golden: core $(BUILD)/tests/run_golden
	@for n in $(GOLDENS); do \
	  $(BUILD)/tests/run_golden --synthetic assets tests/golden/$$n.txt > $(BUILD)/tests/$$n-synthetic.out || exit 1; \
	  diff -u tests/golden/$$n-synthetic.golden $(BUILD)/tests/$$n-synthetic.out || exit 1; \
	  case $$n in mission-*) awk '$$(NF-7) == "ff" { f = 1 } END { exit !f }' $(BUILD)/tests/$$n-synthetic.out || { echo "golden: synthetic $$n never reaches the end code 0xFF (the success path of the mission)"; exit 1; };; esac; \
	  echo "golden: synthetic $$n ok"; \
	  if [ -f assets/models/index.txt ] && [ -f tests/golden/$$n-real.golden ]; then \
	    $(BUILD)/tests/run_golden assets tests/golden/$$n.txt > $(BUILD)/tests/$$n-real.out || exit 1; \
	    diff -u tests/golden/$$n-real.golden $(BUILD)/tests/$$n-real.out || exit 1; \
	    echo "golden: real $$n ok"; \
	  else echo "golden: real $$n skipped (assets or local tests/golden/$$n-real.golden absent; make golden-update-real)"; fi; \
	done

# Regenerates the local real-asset goldens (git-ignored: they are derived from the user's disk).
golden-update-real: core $(BUILD)/tests/run_golden
	@for n in $(GOLDENS); do $(BUILD)/tests/run_golden assets tests/golden/$$n.txt > tests/golden/$$n-real.golden || exit 1; done

# Regenerates the committed synthetic goldens (review the diff before committing).
golden-update-synthetic: core $(BUILD)/tests/run_golden
	@for n in $(GOLDENS); do $(BUILD)/tests/run_golden --synthetic assets tests/golden/$$n.txt > tests/golden/$$n-synthetic.golden || exit 1; done

test: $(TEST_BIN) $(BUILD)/raster_driver $(BUILD)/hud_driver $(BUILD)/sky_driver $(BUILD)/text_driver $(BUILD)/effects_driver $(BUILD)/audio_driver
	@for t in $(TEST_BIN); do echo "$$t"; $$t || exit 1; done

$(BUILD)/raster_driver: tests/frontend/raster_driver.c src/frontend/sdl/raster.c src/frontend/sdl/raster.h src/frontend/sdl/fade.c src/frontend/sdl/fade.h src/core/scene.h $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/frontend/raster_driver.c src/frontend/sdl/raster.c src/frontend/sdl/fade.c -o $@ -lm

# the HUD logic without a window (tests/frontend/test_hud.py): hud.c and panel.c need no SDL (hud.c reads the core's window-row table)
$(BUILD)/hud_driver: tests/frontend/hud_driver.c src/frontend/sdl/hud.c src/frontend/sdl/hud.h src/frontend/sdl/panel.c src/frontend/sdl/panel.h src/frontend/sdl/assets.c src/frontend/sdl/raster.c src/frontend/sdl/raster.h src/core/scene.h src/core/game.h $(BUILD)/libcore.a $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/frontend/hud_driver.c src/frontend/sdl/hud.c src/frontend/sdl/panel.c src/frontend/sdl/assets.c src/frontend/sdl/raster.c $(BUILD)/libcore.a -o $@ -lm

# the sky fill and sea mesh stage for one logged camera state (tests/frontend/test_sky_frames.py)
$(BUILD)/sky_driver: tests/frontend/sky_driver.c src/frontend/sdl/raster.c src/frontend/sdl/raster.h src/core/scene.h src/core/sky.h $(BUILD)/libcore.a $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/frontend/sky_driver.c src/frontend/sdl/raster.c $(BUILD)/libcore.a -o $@ -lm

# the text drawer and the log file without a window (tests/frontend/test_text.py)
$(BUILD)/text_driver: tests/frontend/text_driver.c src/frontend/sdl/text.c src/frontend/sdl/text.h src/frontend/sdl/logfile.c src/frontend/sdl/logfile.h src/frontend/sdl/assets.c src/frontend/sdl/raster.h src/core/scene.h $(BUILD)/libcore.a $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/frontend/text_driver.c src/frontend/sdl/text.c src/frontend/sdl/logfile.c src/frontend/sdl/assets.c $(BUILD)/libcore.a -o $@ -lm

# the combat effects without a window (tests/frontend/test_effects.py)
$(BUILD)/effects_driver: tests/frontend/effects_driver.c src/frontend/sdl/effects.c src/frontend/sdl/effects.h src/frontend/sdl/raster.c src/frontend/sdl/raster.h src/core/scene.h $(BUILD)/libcore.a $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/frontend/effects_driver.c src/frontend/sdl/effects.c src/frontend/sdl/raster.c $(BUILD)/libcore.a -o $@ -lm

# the Paula-like mixer without a device (tests/frontend/test_audio.py)
$(BUILD)/audio_driver: tests/frontend/audio_driver.c src/frontend/sdl/audio.c src/frontend/sdl/audio.h $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tests/frontend/audio_driver.c src/frontend/sdl/audio.c -o $@ -lm

frontend: $(BUILD)/interceptor $(BUILD)/raster_driver $(BUILD)/hud_driver $(BUILD)/sky_driver $(BUILD)/text_driver $(BUILD)/effects_driver $(BUILD)/audio_driver

# The native program carries the invented world of tests/world_synth.h for --synthetic (the device run in CI, which has no assets); the browser build below does not.
$(BUILD)/interceptor: $(FRONTEND_SRC) tests/frontend/synthetic_world.c tests/world_synth.h tests/sound_synth.h tests/ai_tables.h $(BUILD)/libcore.a $(STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DINTERCEPTOR_SYNTHETIC $(SDL_CFLAGS) $(FRONTEND_SRC) tests/frontend/synthetic_world.c $(BUILD)/libcore.a -o $@ $(LDFLAGS) $(SDL_LIBS) -lm

# The browser build (docs: README "Browser version"): no asset in it. The page takes the player's ADF, runs the JavaScript extractor
# (tools/extract-js, copied to extract/) and writes the assets into MEMFS before it calls main. Output: build/web/, ready for static hosting.
WEB := $(BUILD)/web
WEB_EXTRACT_SRC := $(wildcard tools/extract-js/src/*.js)
# WEB_EXPORTS: each entry with its leading comma. The page calls _web_audio_unlock from the Start click (the user gesture the browser wants before it lets sound play); _web_game_end is the game ending itself (ESC on the credits), which calls Module.onGameEnd if the page defines it; _web_log_saved follows every save of the flight log and calls Module.onLogSaved (the page then writes the IDBFS of the log directory, -lidbfs.js).
WEB_EXPORTS := ,_web_audio_unlock,_web_game_end,_web_log_saved
web: $(WEB)/index.html $(WEB)/interceptor.js $(WEB)/page.js $(WEB)/extract/index.js $(WEB)/extract/segments.txt

$(WEB)/interceptor.js: $(FRONTEND_SRC) $(CORE_SRC) $(wildcard src/core/*.h src/frontend/sdl/*.h)
	@mkdir -p $(dir $@)
	EMCC_CORES=1 emcc -std=c99 -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 \
	  -sMODULARIZE=1 -sEXPORT_NAME=createInterceptor -sEXPORTED_RUNTIME_METHODS=FS,callMain -sINVOKE_RUN=0 -sFORCE_FILESYSTEM=1 -lidbfs.js \
	  $(if $(WEB_EXPORTS),-sEXPORTED_FUNCTIONS=_main$(WEB_EXPORTS),) \
	  $(CORE_SRC) $(FRONTEND_SRC) -o $@

$(WEB)/index.html: src/frontend/web/shell.html
	@mkdir -p $(dir $@)
	cp $< $@

$(WEB)/page.js: src/frontend/web/page.js
	@mkdir -p $(dir $@)
	cp $< $@

$(WEB)/extract/index.js: $(WEB_EXTRACT_SRC)
	@mkdir -p $(dir $@)
	cp $(WEB_EXTRACT_SRC) $(dir $@)

$(WEB)/extract/segments.txt: tools/extract-js/segments.txt
	@mkdir -p $(dir $@)
	cp $< $@

clean:
	rm -rf $(BUILD)

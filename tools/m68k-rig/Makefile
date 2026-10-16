# tools/m68k-rig/Makefile
CC     ?= gcc
AR     ?= ar
CFLAGS ?= -O2 -Wall
M      := musashi
# Musashi's m68k.h honours MUSASHI_CNF; this makes the rig's own config the one in force.
CNF    := -DMUSASHI_CNF='"$(CURDIR)/m68kconf.h"'
GEN    := $(M)/m68kops.c $(M)/m68kops.h

all: test_rig

$(M)/m68kmake: $(M)/m68kmake.c
	$(CC) -o $@ $<

$(GEN): $(M)/m68kmake $(M)/m68k_in.c
	cd $(M) && ./m68kmake

librig.a: rig.c rig.h m68kconf.h $(GEN)
	$(CC) $(CFLAGS) $(CNF) -I. -I$(M) -c rig.c -o rig.o
	$(CC) $(CFLAGS) $(CNF) -I. -I$(M) -c $(M)/m68kcpu.c -o m68kcpu.o
	$(CC) $(CFLAGS) $(CNF) -I. -I$(M) -c $(M)/m68kops.c -o m68kops.o
	$(CC) $(CFLAGS) $(CNF) -I. -I$(M) -c $(M)/m68kdasm.c -o m68kdasm.o
	$(CC) $(CFLAGS) $(CNF) -I. -I$(M) -c $(M)/softfloat/softfloat.c -o softfloat.o
	$(AR) rcs $@ rig.o m68kcpu.o m68kops.o m68kdasm.o softfloat.o

# Shared build for the Python model checks (tests/lists_model/random_check.py): same sources, position independent.
librig.so: rig.c rig.h m68kconf.h $(GEN)
	$(CC) $(CFLAGS) -fPIC -shared $(CNF) -I. -I$(M) rig.c $(M)/m68kcpu.c $(M)/m68kops.c $(M)/m68kdasm.c $(M)/softfloat/softfloat.c -o $@ -lm

test_rig: tests/test_rig.c librig.a
	$(CC) $(CFLAGS) -I. $< librig.a -o $@ -lm

# Differential test of the port's fx_* against the original image. Needs the repo's
# ../../build/libcore.a (built here with CC/CFLAGS of the root Makefile defaults).
# core is a FORCE target so the sub-make always runs; the binary relinks whenever libcore.a is newer.
core: FORCE
	$(MAKE) -C ../.. core

FORCE:
.PHONY: FORCE core

test_fx_diff: ../../tests/rig/test_fx_diff.c librig.a ../../src/core/fx.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-diff: test_fx_diff
	./test_fx_diff

# The flight model against the original step routine 0x26AF8 (docs/re/05-flight-model.md). Skips without extracted/interceptor.bin
# or the extracted assets (../../assets/tables/flight_*.bin and the models).
test_flight_diff: ../../tests/rig/test_flight_diff.c ../../tests/flight_examples.h librig.a ../../src/core/flight.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-flight: test_flight_diff
	./test_flight_diff

# The port's training state routines and sequencer against the original's world update for the instructor's record (tests/rig/test_training_diff.c;
# docs/re/08-game-flow.md section 8). Skips without extracted/interceptor.bin. Environment: TRAINING_DIFF_N, TRAINING_DIFF_SEED, TRAINING_DIFF_VERBOSE, TRAINING_DIFF_STATE.
test_training_diff: ../../tests/rig/test_training_diff.c librig.a ../../src/core/training.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-training: test_training_diff
	./test_training_diff

# The instructor's trajectory: the port's training_step with the flight model against the original's world update step by step (tests/rig/test_training_traj.c).
# Skips without extracted/interceptor.bin or the extracted assets. Both parts, 3,000 steps each, from the original's placement. Environment: TRAINING_TRAJ_STEPS, TRAINING_TRAJ_ALL, TRAINING_TRAJ_PART (1 or 2 only).
test_training_traj: ../../tests/rig/test_training_traj.c librig.a ../../src/core/training.h ../../src/core/flight.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-training-traj: test_training_traj
	./test_training_traj

# The port's generic AI (the gate, the speed control, target handling, the evasion choice, the state routine and the steering) against the original's world update for one AI record
# (tests/rig/test_ai_diff.c; docs/re/07-ai-and-weapons.md sections 3 and 4). Skips without extracted/interceptor.bin. Environment: AI_DIFF_N, AI_DIFF_SEED, AI_DIFF_VERBOSE, AI_DIFF_STATE, AI_DIFF_GATE.
test_ai_diff: ../../tests/rig/test_ai_diff.c librig.a ../../src/core/ai.h ../../src/core/entities.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-ai: test_ai_diff
	./test_ai_diff

# The generic AI over time: the port's world update (entities_step with the flight model) against the original's 0x26AF8, step by step, for one AI aircraft chasing a target
# (tests/rig/test_ai_traj.c). Skips without extracted/interceptor.bin or the extracted assets. Environment: AI_TRAJ_STEPS, AI_TRAJ_CASES, AI_TRAJ_SEED, AI_TRAJ_ALL.
test_ai_traj: ../../tests/rig/test_ai_traj.c librig.a ../../src/core/entities.h ../../src/core/ai.h ../../src/core/flight.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-ai-traj: test_ai_traj
	./test_ai_traj
	AI_TRAJ_DIVE=1 AI_TRAJ_STEPS=100 AI_TRAJ_CASES=60 ./test_ai_traj

# The ejection seat's, the parachute's and the pod's step against the original's world update (tests/rig/test_seat_diff.c; docs/re/07-ai-and-weapons.md 6.4). Skips without
# extracted/interceptor.bin or the extracted assets. Environment: SEAT_DIFF_N, SEAT_DIFF_SEED, SEAT_DIFF_VERBOSE.
test_seat_diff: ../../tests/rig/test_seat_diff.c librig.a ../../src/core/entities.h ../../src/core/flight.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-seat: test_seat_diff
	./test_seat_diff
	SEAT_DIFF_SPAWN=1 ./test_seat_diff

# The mission-start placement against the original's 0x2CE36 (tests/rig/test_spawn_diff.c; docs/re/07-ai-and-weapons.md 2.1 to 2.4). Skips without extracted/interceptor.bin or the
# extracted assets. Environment: SPAWN_DIFF_N, SPAWN_DIFF_SEED, SPAWN_DIFF_VERBOSE.
test_spawn_diff: ../../tests/rig/test_spawn_diff.c librig.a ../../src/core/entities.h ../../src/core/screen.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-spawn: test_spawn_diff
	./test_spawn_diff
	SPAWN_DIFF_TARGETS=1 ./test_spawn_diff

# The radar module (the list builder 0x1FEE6, the scope walk 0x35E5E with the threat bits, the cues and the designation, the player's range 0x2835A, the lock state 0x38884, the sight
# 0x385F4 with the pipper tail, the message-line driver 0x36DB2) against the original's routines (tests/rig/test_radar_diff.c; docs/re/07-ai-and-weapons.md section 12). Skips without
# extracted/interceptor.bin. Environment: RADAR_DIFF_N (cases per part, default 1500), RADAR_DIFF_SEED, RADAR_DIFF_VERBOSE, RADAR_DIFF_PART (list, walk, desig, range, lock, sight, line).
test_radar_diff: ../../tests/rig/test_radar_diff.c librig.a ../../src/core/radar.h ../../src/core/entities.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-radar: test_radar_diff
	./test_radar_diff

# The mission dispatcher against the original's end-of-update routine 0x2852E (tests/rig/test_missions_diff.c; docs/re/08-game-flow.md section 12). Skips without extracted/interceptor.bin or
# the extracted assets. Environment: MISSIONS_DIFF_N, MISSIONS_DIFF_SEED, MISSIONS_DIFF_MODE, MISSIONS_DIFF_VERBOSE. Fails on any difference.
test_missions_diff: ../../tests/rig/test_missions_diff.c librig.a ../../src/core/missions.h ../../src/core/entities.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-missions: test_missions_diff
	./test_missions_diff

# the same with the core's AI traced to stderr (debugging a difference): AI_DIFF_ONLY=n ./test_ai_diff_trace
test_ai_diff_trace: ../../tests/rig/test_ai_diff.c ../../src/core/ai.c librig.a ../../src/core/ai.h ../../src/core/entities.h core
	$(CC) $(CFLAGS) -DAI_TRACE -I. $< ../../src/core/ai.c librig.a ../../build/libcore.a -o $@ -lm

# Display-list builder against the emulator's queues (docs/re/04a-display-lists.md). Skips without extracted/ logs.
test_lists_diff: tests/test_lists_diff.c librig.a
	$(CC) $(CFLAGS) -I. $< librig.a -o $@ -lm

test-lists: test_lists_diff
	./test_lists_diff

# The port's display-list builder against the original builder 0x1F770 (tests/rig/test_lists_diff.c; docs/re/04a-display-lists.md). Skips without
# extracted/interceptor.bin or the extracted assets.
test_lists_port_diff: ../../tests/rig/test_lists_diff.c ../../tests/lists_state.h librig.a ../../src/core/lists.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-lists-port: test_lists_port_diff
	./test_lists_port_diff ../../assets

# The port's primitive handlers of the draw layer against the original's (tests/rig/test_draw_diff.c; docs/re/03-3d-pipeline.md "Draw operations"). Skips without
# extracted/interceptor.bin. Environment: DRAW_DIFF_N, DRAW_DIFF_SEED, DRAW_DIFF_VERBOSE, DRAW_DIFF_OPS, DRAW_DIFF_FULL, DRAW_DIFF_EDGE (the second run covers the word negation of -32,768).
test_draw_diff: ../../tests/rig/test_draw_diff.c librig.a ../../src/core/world.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-draw: test_draw_diff
	./test_draw_diff
	DRAW_DIFF_FULL=1 DRAW_DIFF_EDGE=1 DRAW_DIFF_OPS=2,74,48,13,75,14,11,12,6,19,22 DRAW_DIFF_N=5000 ./test_draw_diff
	DRAW_DIFF_FULL=1 DRAW_DIFF_EDGE=1 DRAW_DIFF_OPS=39,40 DRAW_DIFF_N=5000 ./test_draw_diff

# The port's near-ground pass (stage [90], lists_nearground) against the original routine 0x2C0EC (tests/rig/test_nearground_diff.c; docs/re/04-scene-composition.md). Skips without
# extracted/interceptor.bin or the extracted assets (version 14). Environment: NG_DIFF_N, NG_DIFF_SEED, NG_DIFF_VERBOSE.
test_nearground_diff: ../../tests/rig/test_nearground_diff.c librig.a ../../src/core/lists.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-nearground: test_nearground_diff
	./test_nearground_diff

# The port's sky polygon, side flag, zone flags and sea mesh pass against the original routines (tests/rig/test_sky_diff.c; docs/re/04-scene-composition.md).
# Skips without extracted/interceptor.bin or the extracted assets (version 14). Environment: SKY_DIFF_N, SKY_DIFF_SEED.
test_sky_diff: ../../tests/rig/test_sky_diff.c librig.a ../../src/core/sky.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-sky: test_sky_diff
	./test_sky_diff

# The weapons slice against the original (tests/rig/test_weapons_diff.c; docs/re/07-ai-and-weapons.md sections 7 to 10 and 13): a missile through the world update, the gun routine,
# the contact scan and the damage handler. Skips without extracted/interceptor.bin or the extracted assets. Environment: WEAPONS_DIFF_PART, WEAPONS_DIFF_N, WEAPONS_DIFF_STEPS,
# WEAPONS_DIFF_SEED, WEAPONS_DIFF_VERBOSE, WEAPONS_DIFF_ALL, WEAPONS_DIFF_CASE.
test_weapons_diff: ../../tests/rig/test_weapons_diff.c librig.a ../../src/core/weapons.h ../../src/core/entities.h ../../src/core/flight.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-weapons: test_weapons_diff
	./test_weapons_diff
	# the whole-state part reads extracted/captures/state-flight.bin and landmark-state-cases.bin (state_dump.py, landmark_state_check.py) and skips without them; it needs the 14 MB machine
	WEAPONS_DIFF_PART=lmstate ./test_weapons_diff

# The display lists' vertex-slot store against the original's model drawer 0x21C44 (tests/rig/test_slots_diff.c; docs/re/07-ai-and-weapons.md 8.2a): the 58 slots +0xA4.. and the part id of a record
# after one draw, for the models 3 to 10 and 12, random matrices, levels, distances and dying counters. Skips without extracted/interceptor.bin or the extracted assets. Environment: SLOTS_DIFF_N,
# SLOTS_DIFF_SEED, SLOTS_DIFF_VERBOSE, SLOTS_DIFF_DUMP, SLOTS_DIFF_MODELS.
test_slots_diff: ../../tests/rig/test_slots_diff.c librig.a ../../src/core/lists.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-slots: test_slots_diff
	./test_slots_diff

# The ship volumes against the original (tests/rig/test_ship_diff.c; docs/re/07-ai-and-weapons.md 8.2 and 10.2): the gun's big-hull test 0x2B3F0 and the contact scan 0x2B5E2 against ship records
# (the viewed player, a missile that is not the viewed record, a missile that is). Skips without extracted/interceptor.bin or the extracted assets. Environment: SHIP_DIFF_N, SHIP_DIFF_SEED,
# SHIP_DIFF_VERBOSE, SHIP_DIFF_PART.
test_ship_diff: ../../tests/rig/test_ship_diff.c librig.a ../../src/core/flight_int.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-ships: test_ship_diff
	./test_ship_diff

# The port's sound module (src/core/sound.c) against the original's sound library and per-pass routine 0x14AA0, the custom-register write log as the witness
# (tests/rig/test_sound_diff.c; docs/re/10-sound.md). Parts: tone, blank, events, pass. Skips without extracted/interceptor.bin or the extracted assets (version 25).
# Environment: SOUND_DIFF_PART, SOUND_DIFF_N (passes, default 2000), SOUND_DIFF_SEED, SOUND_DIFF_VERBOSE.
test_sound_diff: ../../tests/rig/test_sound_diff.c librig.a ../../src/core/sound.h core
	$(CC) $(CFLAGS) -I. $< librig.a ../../build/libcore.a -o $@ -lm

test-sound-core: test_sound_diff
	./test_sound_diff

# Python model checks (tests/lists_model/, docs/re/04a-display-lists.md, Verified by); they read the user's git-ignored extracted/ files at run
# time and skip cleanly (exit 0) when one is missing.
#   test-lists-scan: the scan, LOD rule, run sort and landmark reorder against the rig's builder on random states (random_check.py SEED N)
#   test-lists-walk: the walkers against the emulator's logged results and the drawer-result model (walk_check.py, all modes)
PYTHON ?= python3

test-lists-scan: librig.so
	@if command -v $(PYTHON) >/dev/null 2>&1; then cd tests/lists_model && $(PYTHON) random_check.py 1 60; else echo "test-lists-scan: skipped (no $(PYTHON))"; fi

test-lists-walk: librig.so
	@if command -v $(PYTHON) >/dev/null 2>&1; then cd tests/lists_model && $(PYTHON) walk_check.py all; else echo "test-lists-walk: skipped (no $(PYTHON))"; fi

# The sound library and its callers against the model of docs/re/10-sound.md (sndmodel.py), the custom-register write log as the witness; the second target compares the captures' logs.
#   test-sound: objects, tone, noise, music, beeps, pass (2,000 passes), ramp; test-sound-captures: the logs of tools/vamiga-ref/scripts/sound-*.txt and, against the port, the milestone-7 logs sound7-*.txt (tools/vamiga-ref/sound7_check.py; both skip without the logs)
test-sound: librig.so
	@if command -v $(PYTHON) >/dev/null 2>&1; then cd tests/lists_model && ( ulimit -v 2500000 2>/dev/null; $(PYTHON) sound_check.py all ); else echo "test-sound: skipped (no $(PYTHON))"; fi

test-sound-captures:
	@if command -v $(PYTHON) >/dev/null 2>&1; then cd tests/lists_model && $(PYTHON) sound_capture_check.py all; else echo "test-sound-captures: skipped (no $(PYTHON))"; fi
	@if command -v $(PYTHON) >/dev/null 2>&1 && [ -x ../../build/sound7_port ]; then cd ../.. && $(PYTHON) tools/vamiga-ref/sound7_check.py; else echo "sound7 captures: skipped (no $(PYTHON) or build/sound7_port: cc -std=c99 -O1 tools/vamiga-ref/sound7_port.c build/libcore.a -o build/sound7_port -lm)"; fi

test: test_rig test-diff test-flight test-training test-ai test-radar test-weapons test-slots test-ships test-ai-traj test-seat test-spawn test-missions test-lists test-lists-port test-draw test-nearground test-sky test-lists-scan test-lists-walk test-sound test-sound-core
	./test_rig

clean:
	rm -f *.o librig.a librig.so test_rig test_fx_diff test_flight_diff test_lists_diff test_lists_port_diff test_draw_diff test_nearground_diff test_sky_diff test_training_diff test_training_traj test_ai_diff test_radar_diff test_ai_traj test_seat_diff test_spawn_diff test_missions_diff test_ai_diff_trace $(GEN) $(M)/m68kmake test_weapons_diff test_weapons_diff_trace test_slots_diff test_ship_diff test_sound_diff

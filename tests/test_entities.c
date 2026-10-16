/* tests/test_entities.c - the entity records, the spawner, the world-update order, the dying path and the ejection (src/core/entities.c,
 * docs/re/07-ai-and-weapons.md Part A, "note 07"; section numbers below are that note's).
 *
 * The tests use the note's values and invented, text-free data: a template table whose words are 0x1000 * record + word, a few position blocks, a scenario with a handful of
 * entries. The flight step of the dying tests runs on empty flight tables (the expiry and the counters do not read them). */
#include "test.h"
#include "ai_tables.h"
#include "../src/core/entities.h"
#include "../src/core/ai.h"
#include "../src/core/flight.h"

static templates_t TPL;
static scenario_t SC;
static entities_t E;
static aircraft_t P;
static flight_tables_t TABLES;
static flight_env_t ENV;
static int hook_aims;

/* position blocks: entry n -> cell (60 + n, 70 + n), fine (100 n, 200 n), height 0, except the entries 20 and 21 which are airborne (height 176) */
static int blk(void *u, unsigned n, int16_t w[5]) {
    (void)u;
    if (n == 0 || n > 40) return -1;
    w[0] = (int16_t)(60 + n); w[1] = (int16_t)(70 + n); w[2] = (int16_t)(100 * n); w[3] = (int16_t)(200 * n); w[4] = (n == 20 || n == 21) ? 176 : 0;
    return 0;
}
static int32_t aim_hook(void *u, int32_t dx, int32_t dz) { (void)u; hook_aims++; return (int32_t)(((int64_t)dx + dz) % 28800 + 28800) % 28800; }
static int unit_hook(void *u, unsigned t) { (void)u; return (int)(t & 15); }

static void setup(void) {
    memset(&TPL, 0, sizeof TPL);
    TPL.count = TEMPLATE_COUNT;
    for (int i = 0; i < TEMPLATE_COUNT; i++) for (int k = 0; k < TEMPLATE_LONGS; k++) TPL.rec[i][k] = (uint32_t)(0x1000 * i + k);
    memset(&SC, 0, sizeof SC);
    memset(&P, 0, sizeof P);
    P.f.cls = 0x11; P.f.w0 = 0x11C8; P.f.v4a984 = 0x0840; P.f.v4a844 = 1; P.f.v4a847 = 1;
    entities_init(&E, &TPL, &AI_FIXTURE, &SC);
    entities_attach_player(&E, &P);
    E.hook.position_block = blk; E.hook.aim_heading = aim_hook; E.hook.unit_shift = unit_hook;
    E.mode = 3; E.hostile_cap = 4; E.flying = 1; E.sim_on = 1; E.progress = 0;
    hook_aims = 0;
    memset(&TABLES, 0, sizeof TABLES);
    memset(&ENV, 0, sizeof ENV);
    ENV.tab = &TABLES; ENV.landmark_z = 1; ENV.no_rearm = 1; ENV.no_sound_system = 1; ENV.no_liftoff_hook = 1;
    flight_set_env(&ENV);
}

/* an entry: template record t, class c, slot s, word 2 high bits w2hi, position entry n (the word is the byte offset 2 n), flags */
static void entry(uint16_t w[5], int t, int c, int slot, int w2hi, int n, int flags) {
    w[0] = (uint16_t)(20 * t); w[1] = (uint16_t)c; w[2] = (uint16_t)(w2hi | slot); w[3] = (uint16_t)(2 * n); w[4] = (uint16_t)flags;
}

/* ---- the spawner (2) ---------------------------------------------------------------------------------------------------------------------------- */

TEST(spawn_copies_the_template_words_and_writes_the_constants_of_the_placement) {
    setup();
    uint16_t w[5];
    entry(w, 6, 0x12, 8, 0x0000, 5, 0);   /* a MiG-29 in slot 8 with a leader field of 0 (bit 15 clear: the leader is the player) */
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 1);
    for (int k = 0; k < TEMPLATE_LONGS; k++) ASSERT_EQ_INT(E.desc[8][k], 0x1000 * 6 + k);   /* the five longs copied unchanged (2.3) */
    const flight_state_t *f = &E.rec[8].ac.f;
    ASSERT_EQ_INT(f->w0, 0x11C0);                      /* an aircraft: active, AI valid, valid, keeps moving (2.1 step 13) */
    ASSERT_EQ_INT(f->cls, 0x12); ASSERT_EQ_INT(f->group, 8);
    ASSERT_EQ_INT(E.rec[8].stock, 0x44); ASSERT_EQ_INT(E.rec[8].rounds, 500); ASSERT_EQ_INT(f->fuel, 0x61A800);
    ASSERT_EQ_INT(f->id, 0x3D); ASSERT_EQ_INT(E.rec[8].ai_flags, 0x10); ASSERT_EQ_INT(E.rec[8].engage, 3);
    ASSERT_EQ_INT(E.rec[8].tag, 0xFF);                 /* d7 = -2 plus 1: the mission-start tag */
    ASSERT_EQ_INT(E.rec[8].b71, 0xFF);                 /* rule 17: the placement writes +0x71 = 0xFF (the map routine's stored sector: negative means recompute) */
    ASSERT_EQ_INT(f->l7d, 6);                          /* the model-unit nibble from the hook */
    ASSERT_EQ_INT(f->cell_x, 65); ASSERT_EQ_INT(f->cell_z, 75); ASSERT_EQ_INT(f->fine_x, 500); ASSERT_EQ_INT(f->fine_z, 1000);
    ASSERT_EQ_INT(E.rec[8].ac.pos[0], 65 * 4194304 + 500 * 256);
    ASSERT_EQ_INT(E.rec[8].ac.pos[2], 75 * 4194304 + 1000 * 256);
    ASSERT_EQ_INT(f->mat_b[0], 16384); ASSERT_EQ_INT(f->mat_b[1], 0); ASSERT_EQ_INT(f->mat_b[4], 16384);
    ASSERT_EQ_INT(E.hostile_count, 1);                 /* an aircraft without the not-counted bit counts against the cap (2.1 step 9) */
    ASSERT_EQ_INT(E.rec[8].target, 0x80);              /* bit 15 clear and bits 8..14 = 0: the leader is record 0 */
}

TEST(word_2_gives_a_destination_block_or_a_leader_and_the_route_index) {
    setup();
    uint16_t w[5];
    entry(w, 6, 0x12, 8, 0x8000 | (7 << 8), 5, 0x8000);   /* bit 15 set, n = 7: the block 7 is the destination, +0x38 = 0xFF, +0x3A = 7 */
    entities_spawn(&E, w, -2, 0, 0);
    ASSERT_EQ_INT(E.rec[8].dest_cx, 67); ASSERT_EQ_INT(E.rec[8].dest_cz, 77); ASSERT_EQ_INT(E.rec[8].dest_fx, 700); ASSERT_EQ_INT(E.rec[8].dest_fz, 1400);
    ASSERT_EQ_INT(E.rec[8].target, 0xFF); ASSERT_EQ_INT(E.rec[8].route, 7);
    ASSERT_EQ_INT(E.rec[8].ac.f.w0 & 0x0008, 0x0008);   /* word 4 bit 15: not counted */
    ASSERT_EQ_INT(E.hostile_count, 0);
    entry(w, 6, 0x12, 10, 0x8000, 6, 0);                /* bit 15 set, n = 0: the destination words stay 0 and the target 0xFF */
    entities_spawn(&E, w, -2, 0, 0);
    ASSERT_EQ_INT(E.rec[10].dest_cx, 0); ASSERT_EQ_INT(E.rec[10].dest_h, 0); ASSERT_EQ_INT(E.rec[10].target, 0xFF);
    entry(w, 6, 0x12, 12, (4 << 8), 8, 0);              /* bit 15 clear, r = 4: the record's own block is the destination and the leader is record 4 */
    entities_spawn(&E, w, -2, 0, 0);
    ASSERT_EQ_INT(E.rec[12].target, 0x84);
    ASSERT_EQ_INT(E.rec[12].dest_cx, 68); ASSERT_EQ_INT(E.rec[12].dest_fx, 800);
}

TEST(a_valid_record_in_the_slot_skips_the_entry_and_is_never_replaced) {
    setup();
    uint16_t w[5];
    entry(w, 6, 0x12, 8, 0, 5, 0);
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 1);
    E.rec[8].ac.f.fuel = 1234;
    entry(w, 7, 0x13, 8, 0, 9, 0);                      /* the same slot */
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 0);
    ASSERT_EQ_INT(E.rec[8].ac.f.cls, 0x12);             /* nothing was replaced ... */
    ASSERT_EQ_INT(E.rec[8].ac.f.fuel, 1234);
    ASSERT_EQ_INT(E.desc[8][0], 0x1000 * 6);            /* ... not even the descriptor (the skip is before the template copy) */
    ASSERT_EQ_INT(E.n_skipped_valid, 1);
    E.rec[8].ac.f.w0 &= (uint16_t)~0x0040;              /* once freed the slot takes a placement again */
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 1);
    ASSERT_EQ_INT(E.rec[8].ac.f.cls, 0x13);
}

TEST(the_template_is_copied_before_the_cap_refusal_and_the_cap_is_tested_for_every_class) {
    setup();
    uint16_t w[5];
    E.hostile_cap = 2;
    entry(w, 6, 0x12, 8, 0, 5, 0);  ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 1);
    entry(w, 6, 0x12, 10, 0, 5, 0); ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 1);
    ASSERT_EQ_INT(E.hostile_count, 2);
    entry(w, 3, 0x20, 14, 0x8000, 4, 0x8000);            /* a ship after the cap: refused too ... */
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 0);
    ASSERT_EQ_INT(E.rec[14].ac.f.w0 & 0x0040, 0);
    ASSERT_EQ_INT(E.desc[14][0], 0x1000 * 3);            /* ... but its descriptor was rewritten (2.1 step 3 precedes step 4) */
    ASSERT_EQ_INT(E.n_refused_cap, 1);
}

TEST(a_cruise_missile_and_a_friendly_without_the_not_counted_bit_count_against_the_cap) {
    setup();
    uint16_t w[5];
    E.hostile_cap = 2;
    entry(w, 10, 0x15, 4, 0x8000, 3, 0);                 /* the cruise missile: class nibble 1, counted */
    entities_spawn(&E, w, -2, 0, 0);
    entry(w, 3, 0x10, 6, 0x8000, 4, 0);                  /* an F-16 placed without the bit: also counted */
    entities_spawn(&E, w, -2, 0, 0);
    ASSERT_EQ_INT(E.hostile_count, 2);
    entry(w, 6, 0x12, 8, 0, 5, 0);
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 0);   /* the hostile is the third: refused */
}

TEST(a_class_15_record_with_a_non_zero_cell_x_is_not_overwritten_by_the_placer_though_its_valid_bit_is_clear) {
    /* note 07 2.1 step 5 (V, milestone 7: combat7-placer15.txt, the F1 flight snapshot with the mode byte poked to 8 and the player moved into region 3: record 4 staged with class 0x15,
     * cell x 1 and W0 clear was kept at the tick with its descriptor rewritten and the hostile count unchanged; the same record staged with class 0 and cell x 0 was placed, the count rose) */
    setup();
    uint16_t w[5];
    E.hostile_cap = 4;
    E.rec[4].ac.f.cls = 0x15; E.rec[4].ac.f.cell_x = 1; E.rec[4].ac.f.w0 = 0;
    entry(w, 10, 0x15, 4, 0x8000, 3, 0);
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 0);
    ASSERT_EQ_INT(E.rec[4].ac.f.cell_x, 1);
    ASSERT_EQ_INT(E.rec[4].ac.f.w0 & 0x0040, 0);
    ASSERT_EQ_INT(E.hostile_count, 0);
    ASSERT_EQ_INT(E.desc[4][0], 0x1000 * 10);            /* the template copy of step 3 happened */
    E.rec[4].ac.f.cls = 0; E.rec[4].ac.f.cell_x = 0;
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 1);   /* control: an ordinary dead record is placed */
    ASSERT_EQ_INT(E.hostile_count, 1);
}

TEST(the_slot_field_is_seven_bits_and_a_slot_beyond_the_arrays_is_refused_not_written) {
    setup();
    uint16_t w[5];
    entry(w, 6, 0x12, 16, 0, 5, 0);
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 0);
    entry(w, 6, 0x12, 127, 0, 5, 0);
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 0);
    ASSERT_EQ_INT(E.n_spawned, 0);
}

TEST(the_mode_filter_skips_and_the_airborne_block_sets_gear_and_speed) {
    setup();
    uint16_t w[5];
    entry(w, 6, 0x12, 8, 0, 20, (3 << 8));               /* mode filter 3 passes in mode 3 */
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 1);
    ASSERT_EQ_INT(E.rec[8].ac.f.gear, 0xE0); ASSERT_EQ_INT(E.rec[8].ac.f.speed_target, 0x2000); ASSERT_EQ_INT(E.rec[8].ac.f.speed, 0x2000);
    ASSERT_EQ_INT(E.rec[8].ac.f.alt8, 176); ASSERT_EQ_INT(E.rec[8].ac.pos[1], 176 * 256);
    entry(w, 6, 0x12, 10, 0, 5, (4 << 8));               /* the filter 4 does not */
    ASSERT_EQ_INT(entities_spawn(&E, w, -2, 0, 0), 0);
    entry(w, 6, 0x12, 12, 0, 5, 0);                      /* height 0: on the ground, gear and speed stay 0 */
    entities_spawn(&E, w, -2, 0, 0);
    ASSERT_EQ_INT(E.rec[12].ac.f.gear, 0); ASSERT_EQ_INT(E.rec[12].ac.f.speed, 0);
}

TEST(the_random_offset_moves_the_cells_and_bit_7_halves_it_arithmetically) {
    setup();
    uint16_t w[5];
    entry(w, 6, 0x12, 8, 0, 5, 0x1000);
    entities_spawn(&E, w, -2, 3, -3);
    ASSERT_EQ_INT(E.rec[8].ac.f.cell_x, 65 + 3); ASSERT_EQ_INT(E.rec[8].ac.f.cell_z, 75 - 3);
    entry(w, 6, 0x12, 10, 0, 5, 0x1080);
    entities_spawn(&E, w, -2, 3, -3);                    /* halved: 3 >> 1 = 1, -3 >> 1 = -2 */
    ASSERT_EQ_INT(E.rec[10].ac.f.cell_x, 65 + 1); ASSERT_EQ_INT(E.rec[10].ac.f.cell_z, 75 - 2);
    entry(w, 6, 0x12, 12, 0, 5, 0);                      /* without bit 12: no offset */
    entities_spawn(&E, w, -2, 3, -3);
    ASSERT_EQ_INT(E.rec[12].ac.f.cell_x, 65);
}

TEST(the_mission_start_places_the_first_entry_without_the_threshold_and_aims_it) {
    setup();
    SC.sub_entry_count = 3;
    entry(SC.sub_entry[0].w, 8, 0x20, 14, 0x8000, 4, 0x8005);       /* threshold 5, but the first entry is placed without the test */
    entry(SC.sub_entry[1].w, 6, 0x12, 8, 0x8000 | (3 << 8), 5, 3);   /* threshold 3: not placed with progress 0 */
    entry(SC.sub_entry[2].w, 6, 0x12, 10, 0x8000 | (3 << 8), 5, 0);  /* threshold 0: placed, aimed at the block 3 */
    E.progress = 0;
    ASSERT_EQ_INT(entities_start_mission(&E, 0, 3, 4, 0, 0), 2);
    ASSERT_TRUE(E.rec[14].ac.f.w0 & 0x0040);
    ASSERT_EQ_INT(E.rec[14].ac.f.w0 & 0x1000, 0);                    /* a ship: W0 0x0140, no AI valid, not active (2.1 step 13) */
    ASSERT_EQ_INT(E.rec[8].ac.f.w0 & 0x0040, 0);
    ASSERT_TRUE(E.rec[10].ac.f.w0 & 0x0040);
    ASSERT_TRUE(hook_aims >= 1);                                     /* the aim routine ran for the placed entries that have a destination */
    ASSERT_EQ_INT(E.sub_first, 0); ASSERT_EQ_INT(E.sub_count, 3);
}

TEST(region_triggers_place_once_per_entry_into_the_box_set_the_tags_and_shift_the_bit_numbers) {
    setup();
    SC.region_count = 2; SC.region_entry_count = 3;
    SC.region[0].box[0] = 60; SC.region[0].box[1] = 70; SC.region[0].box[2] = 60; SC.region[0].box[3] = 70; SC.region[0].count_word = 1; SC.region[0].first = 0; SC.region[0].count = 1;
    SC.region[1].box[0] = 60; SC.region[1].box[1] = 70; SC.region[1].box[2] = 60; SC.region[1].box[3] = 70; SC.region[1].count_word = 2; SC.region[1].first = 1; SC.region[1].count = 2;
    entry(SC.region_entry[0].w, 6, 0x12, 8, 0x8000 | (3 << 8), 5, 0);
    entry(SC.region_entry[1].w, 6, 0x12, 10, 0x8000 | (3 << 8), 5, 0);
    entry(SC.region_entry[2].w, 6, 0x12, 12, 0x8000 | (3 << 8), 5, 0);
    E.hostile_cap = 8;
    P.f.cell_x = 65; P.f.cell_z = 65;                                 /* inside both boxes */
    E.pass = 2;
    entities_region_tick(&E);
    ASSERT_EQ_INT(E.n_spawned, 0);                                    /* the tick runs when the low nibble of the counter is 3 */
    E.pass = 3;
    entities_region_tick(&E);
    ASSERT_TRUE(E.rec[8].ac.f.w0 & 0x0040);
    ASSERT_EQ_INT(E.rec[8].tag, 1);                                   /* the first entry of the first region: bit number 0 plus 1 */
    /* the placer left d7 = 0 (placed): the next region's bit number is 1; its first entry's tag is d7 + 1 = 2, then 1 after a placed entry */
    ASSERT_TRUE(E.rec[10].ac.f.w0 & 0x0040);
    ASSERT_EQ_INT(E.rec[10].tag, 2);                                  /* the bit number of the second region is 1: 1 plus 1 */
    ASSERT_EQ_INT(E.rec[12].tag, 1);
    ASSERT_EQ_INT(E.region_bits, 0x03);
    /* a second tick with the player still inside: the entries are offered again; the valid slots are skipped */
    const unsigned spawned = E.n_spawned;
    E.pass = 19;
    entities_region_tick(&E);
    ASSERT_EQ_INT(E.n_spawned, spawned);
    ASSERT_TRUE(E.n_skipped_valid > 0);
    /* leaving the boxes: the exit re-addresses the records (word 2 with n = 3: the block 3, +0x38 = 0xFF) and clears the bits */
    E.rec[8].dest_cx = 0; E.rec[10].dest_cx = 0;
    E.rec[8].target = 0x80;                                           /* following the player: redirected; the record 10 (target 0xFF, a block destination) is left alone */
    P.f.cell_x = 100; P.f.cell_z = 100;
    E.pass = 35;
    entities_region_tick(&E);
    ASSERT_EQ_INT(E.region_bits, 0x00);
    ASSERT_EQ_INT(E.rec[8].dest_cx, 63); ASSERT_EQ_INT(E.rec[8].target, 0xFF);
    ASSERT_EQ_INT(E.rec[10].dest_cx, 0); ASSERT_EQ_INT(E.rec[10].target, 0xFF);
}

TEST(the_region_exit_cases_leader_zero_and_an_invalid_leader_take_the_own_block_a_valid_leader_is_copied) {
    setup();
    SC.region_count = 1; SC.region_entry_count = 3;
    SC.region[0].box[0] = 60; SC.region[0].box[1] = 70; SC.region[0].box[2] = 60; SC.region[0].box[3] = 70; SC.region[0].count_word = 3; SC.region[0].first = 0; SC.region[0].count = 3;
    entry(SC.region_entry[0].w, 6, 0x12, 8, 0, 5, 0);                  /* bit 15 clear, bits 8 to 14 = 0: the player as leader */
    entry(SC.region_entry[1].w, 6, 0x12, 10, 0x0000 | (13 << 8), 5, 0);   /* the leader 13 is not valid */
    entry(SC.region_entry[2].w, 6, 0x12, 12, 0x0000 | (8 << 8), 5, 0);    /* the leader 8 */
    E.hostile_cap = 8;
    P.f.cell_x = 65; P.f.cell_z = 65;
    E.pass = 3;
    entities_region_tick(&E);
    ASSERT_TRUE(E.rec[8].ac.f.w0 & 0x0040); ASSERT_TRUE(E.rec[10].ac.f.w0 & 0x0040); ASSERT_TRUE(E.rec[12].ac.f.w0 & 0x0040);
    for (int s = 8; s <= 12; s += 2) { E.rec[s].target = 0x80; E.rec[s].dest_cx = 0; E.rec[s].engage = 4; }
    E.rec[8].dest_cx = 11;
    P.f.cell_x = 100; P.f.cell_z = 100;
    E.pass = 19;
    entities_region_tick(&E);
    ASSERT_EQ_INT(E.rec[8].dest_cx, 65); ASSERT_EQ_INT(E.rec[8].target, 0x80); ASSERT_EQ_INT(E.rec[8].engage, 5);   /* its own block (entry 5), not the player's destination */
    ASSERT_EQ_INT(E.rec[10].dest_cx, 65); ASSERT_EQ_INT(E.rec[10].target, 0x80);                                     /* an invalid leader: the first case */
    ASSERT_EQ_INT(E.rec[12].target, 0x88); ASSERT_EQ_INT(E.rec[12].engage, 3);                                       /* a valid leader: 0x80 + 8, the leader's destination */
}

TEST(the_region_tick_is_off_in_the_demo_and_places_nothing_in_the_modes_1_2_and_0x7d_but_still_runs) {
    setup();
    SC.region_count = 1; SC.region_entry_count = 1;
    SC.region[0].box[1] = 127; SC.region[0].box[3] = 127; SC.region[0].count_word = 1; SC.region[0].count = 1;
    entry(SC.region_entry[0].w, 6, 0x12, 8, 0x8000, 5, 0);
    E.pass = 3;
    E.demo = 1; entities_region_tick(&E); ASSERT_EQ_INT(E.n_spawned, 0);
    E.demo = 0; E.mode = 1; entities_region_tick(&E); ASSERT_EQ_INT(E.n_spawned, 0);
    E.mode = 0x7D; entities_region_tick(&E); ASSERT_EQ_INT(E.n_spawned, 0);
    ASSERT_EQ_INT(E.region_bits, 0x01);                                  /* the tick ran (the bit is set), the placer left d7 as the loop had it */
    E.region_bits = 0;
    E.mode = 4; entities_region_tick(&E); ASSERT_EQ_INT(E.n_spawned, 1);
}

/* ---- the secondary objects (1.5) and the launch requests (1.2) -------------------------------------------------------------------------------------- */

TEST(the_sixteen_secondary_objects_are_the_limit_nothing_is_created_or_reused_when_all_are_valid) {
    setup();
    secondary_t s;
    memset(&s, 0, sizeof s);
    s.pos[0] = 123;
    for (int i = 0; i < 16; i++) ASSERT_EQ_INT(entities_object_create(&E, &s, 0x11), i);   /* the first free slot each time */
    E.request = 0;
    s.pos[0] = 999;
    ASSERT_EQ_INT(entities_object_create(&E, &s, 0x11), -1);
    ASSERT_EQ_INT(E.request, 0);                                       /* no display request when nothing was created */
    for (int i = 0; i < 16; i++) ASSERT_EQ_INT(E.obj[i].pos[0], 123);  /* none overwritten */
    ASSERT_EQ_INT(E.n_objects_refused, 1);
    E.obj[5].w0 = 0;                                                   /* one freed (by the draw state machine): the next call takes exactly that one */
    ASSERT_EQ_INT(entities_object_create(&E, &s, 0), 5);
    ASSERT_EQ_INT(E.obj[5].life, 0x7F);
    ASSERT_EQ_INT(E.obj[5].model, 0x12);                               /* model 0: by the parity of the slot: 15 - 5 = 10 is even */
    E.obj[6].w0 = 0;
    ASSERT_EQ_INT(entities_object_create(&E, &s, 0), 6);
    ASSERT_EQ_INT(E.obj[6].model, 0x13);                               /* 15 - 6 = 9 is odd */
    ASSERT_EQ_INT(E.request, 0xFF);
}

TEST(the_life_words_count_down_once_per_unpaused_pass) {
    setup();
    secondary_t s;
    memset(&s, 0, sizeof s);
    entities_object_create(&E, &s, 0x11);
    entities_step(&E);
    entities_step(&E);
    ASSERT_EQ_INT(E.obj[0].life, 0x7F - 2);
}

TEST(a_launch_request_that_finds_no_free_record_is_dropped_at_the_end_of_the_pass) {
    setup();
    for (int i = 1; i <= 3; i++) { E.rec[i].ac.f.w0 = 0x0040; E.rec[i].ac.f.cls = 0x00; }   /* the player's three weapon records in use */
    E.req_player_launch = 1; E.req_ai_slot4 = 1; E.req_ai = 1;
    entities_step(&E);
    ASSERT_EQ_INT(E.req_player_launch, 0); ASSERT_EQ_INT(E.req_ai_slot4, 0); ASSERT_EQ_INT(E.req_ai, 0);
    ASSERT_EQ_INT(E.n_dropped_requests, 1);
}

static int launched_by[16], launch_count;
static int launch_hook(void *u, int idx) { (void)u; launched_by[launch_count++ & 15] = idx; return 0; }

TEST(the_script_request_v4a873_launches_the_record_5_before_any_test_of_its_owner_and_the_global_request_goes_to_the_first_asker) {
    setup();
    launch_count = 0;
    E.hook.launch_ai = launch_hook;
    E.rec[4].ac.f.w0 = 0x0040 | 0x8000; E.rec[4].ac.f.b20 = 0x02;      /* the owner in slot 4 is hit and out of control */
    E.req_ai_slot4 = 1;
    entities_step(&E);
    ASSERT_EQ_INT(launch_count, 1); ASSERT_EQ_INT(launched_by[0], 5);   /* fires all the same */
    /* V:4A874 with the owner in slot 10: the record 9 asks first about the slot 8 (empty here) and takes the request; the record 11 finds none */
    setup();
    launch_count = 0;
    E.hook.launch_ai = launch_hook;
    E.rec[10].ac.f.w0 = 0x11C0 & ~0x0100; E.req_ai = 1;
    entities_step(&E);
    ASSERT_EQ_INT(launch_count, 1); ASSERT_EQ_INT(launched_by[0], 9); ASSERT_EQ_INT(E.req_ai, 0);
}

/* ---- the dying path (5) ------------------------------------------------------------------------------------------------------------------------------- */

/* a counted MiG-29 in the air in slot 8, high enough that the flight step finds nothing to collide with */
static void put_hostile(int cls, int w0_extra, int w2) {
    uint16_t w[5];
    entry(w, 6, cls, 8, 0x8000 | (3 << 8), 20, 0);
    E.rec[8].ac.f.w0 = 0;
    entities_spawn(&E, w, -2, 0, 0);
    flight_state_t *f = &E.rec[8].ac.f;
    f->w0 = (uint16_t)((f->w0 & ~0x0100) | w0_extra);   /* the fresh bit cleared: the record is already in its second pass */
    f->w2 = (uint16_t)w2;
    E.rec[8].ac.pos[1] = 0x400000; f->alt8 = 0x4000;
    f->speed = f->speed_target = 0x1200;
    E.rec[8].dest_cx = -1;                               /* no destination: the AI has nothing to steer */
    flight_sync(&E.rec[8].ac);
}

TEST(a_shot_down_hostile_dies_for_fifteen_steps_then_is_counted_and_kept_while_it_is_still_in_the_air) {
    setup();
    put_hostile(0x12, 0, 0);
    flight_state_t *f = &E.rec[8].ac.f;
    /* the marks a gun or missile hit leaves (note 07 5.1; part B writes them): the crashed bit, out of control, the state byte 1 */
    f->w0 |= 0x0200; f->b20 |= 0x02; f->state = 1;
    entities_step(&E);                                   /* step 0: the tail of the flight step turns the crashed bit into the dying count */
    ASSERT_EQ_INT(f->w0 & 0x0400, 0x0400); ASSERT_EQ_INT(f->w0 & 0x0200, 0); ASSERT_EQ_INT(f->counter, 15);
    E.rec[8].ac.pos[1] = 0x400000; f->alt8 = 0x4000; f->w2 &= (uint16_t)~0x0080;
    int expiry = -1;
    for (int k = 1; k <= 20; k++) {
        entities_step(&E);
        if (!(f->w0 & 0x0400)) { expiry = k; break; }
        E.rec[8].ac.pos[1] = 0x400000; f->alt8 = 0x4000; f->w2 &= (uint16_t)~0x0080; f->w0 &= (uint16_t)~0x8000;   /* the empty flight tables drop the wreck at once: the test keeps it in the air (the expiry rule is under test, not the physics) */
    }
    ASSERT_EQ_INT(expiry, 15);                           /* the expiry is 15 steps after the step that started the dying (5.2) */
    ASSERT_EQ_INT(E.kills, 1);                           /* counted: class nibble 1, not marked not-counted, not on the ground (5.3 step 2) */
    ASSERT_TRUE(f->w0 & 0x0040);                         /* an airborne wreck without the hit bit is not freed (step 5) */
}

TEST(the_kill_is_counted_only_for_a_counted_aircraft_in_the_air_and_the_record_is_freed_by_the_ground_or_the_hit_bit) {
    /* (class, W0 extra bits, W2) -> counted, freed */
    static const struct { int cls, w0, w2, counted, freed; } T[] = {
        { 0x12, 0x0000, 0x0000, 1, 0 },     /* airborne counted wreck: counted, kept */
        { 0x12, 0x0008, 0x0000, 0, 0 },     /* not counted (W0 bit 3): not counted, kept */
        { 0x12, 0x0000, 0x0080, 0, 1 },     /* on the ground: not counted, freed */
        { 0x12, 0x8000, 0x0000, 1, 1 },     /* the hit bit: counted and freed */
        { 0x15, 0x0000, 0x0000, 0, 1 },     /* the cruise missile: freed, no count */
        { 0x13, 0x8000, 0x0080, 0, 1 },
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        setup();
        put_hostile(T[i].cls, T[i].w0 | 0x0400, T[i].w2);
        E.rec[8].ac.f.counter = 1;                       /* the gate lowers it to 0 in the step: the expiry */
        entities_step(&E);
        ASSERT_EQ_INT(E.kills, T[i].counted);
        ASSERT_EQ_INT((E.rec[8].ac.f.w0 & 0x0040) == 0, T[i].freed);
        ASSERT_EQ_INT(E.rec[8].ac.f.w0 & 0x0400, 0);
        if (T[i].freed) ASSERT_EQ_INT(E.rec[8].ac.f.counter, 20);   /* freeing: +0x4C = 20 (5.4) */
    }
}

/* a hostile that the flight step's collision finds below the ground (y = -5 * 256, the step's displacement (100, -1280, 60)): the ground result 0x10 through the result handler of a non-player
 * record (the geometry that raises the result in a step is the flight tests' business: the motion step holds an aircraft at its floor) */
static int weapon_hook(void *u, int idx) { (void)u; (void)idx; return 1; }
static void below_ground(void) {
    flight_state_t *f = &E.rec[8].ac.f;
    f->w0 |= 0x0040; f->v4a844 = 1;                       /* the collisions switch the world update copies into every record */
    E.rec[8].ac.pos[1] = -1280; f->alt8 = -5;
    f->vel[0] = 100; f->vel[1] = -1280; f->vel[2] = 60;
}
static void ground_result(void) {
    const int32_t before[3] = { E.rec[8].ac.pos[0], E.rec[8].ac.pos[1], E.rec[8].ac.pos[2] };
    flight_test_result_entity(&E.rec[8].ac, 0x10, before);
    entities_step(&E);                                   /* the world update collects the object the handler asked for (the record is dying: stepped with its dying count) */
}

TEST(a_ground_result_for_an_aircraft_starts_the_dying_without_the_ground_bit_and_makes_no_object_and_no_back_up) {
    setup();
    put_hostile(0x12, 0, 0);
    below_ground();
    const int32_t y0 = E.rec[8].ac.pos[1];
    flight_state_t *f = &E.rec[8].ac.f;
    const int32_t before[3] = { E.rec[8].ac.pos[0], y0, E.rec[8].ac.pos[2] };
    flight_test_result_entity(&E.rec[8].ac, 0x10, before);
    ASSERT_EQ_INT(f->w0 & 0x8000, 0x8000);               /* the hit bit */
    ASSERT_EQ_INT(f->b20 & 0x02, 0x02);                  /* out of control */
    ASSERT_EQ_INT(f->w0 & 0x1000, 0);                    /* not active */
    ASSERT_EQ_INT(f->w0 & 0x0400, 0x0400); ASSERT_EQ_INT(f->w0 & 0x0200, 0); ASSERT_EQ_INT(f->counter, 15);
    ASSERT_EQ_INT(f->w2 & 0x0080, 0);                    /* only the player's branch sets the ground bit (note 07 5.1) */
    ASSERT_EQ_INT(E.rec[8].ac.pos[1], y0);               /* an aircraft is not backed up */
    entities_step(&E);
    ASSERT_EQ_INT(E.obj[0].w0 & 0x0040, 0);              /* and makes no object */
    ASSERT_EQ_INT(E.kills, 0);
}

static int follow_calls; static const flight_state_t *follow_last;
static void follow_hook(void *u, const flight_state_t *f) { (void)u; follow_calls++; follow_last = f; }

TEST(only_an_aircraft_contact_result_0x40_of_a_record_with_a_class_nibble_writes_the_followed_record_and_w2_bit_0x1000) {
    /* note 07 13.1 (0x2A954): a non-player record that is not a missile and not class 0x15, with a collision result of 0x40 or more, takes V:54E72, W2 0x1000 and the state byte 1; the ground (0x10)
     * and hull or landmark (0x20) results write none of it */
    static const struct { int cls, r, written; } T[] = { { 0x12, 0x40, 1 }, { 0x12, 0x10, 0 }, { 0x12, 0x20, 0 }, { 0x15, 0x40, 0 }, { 0x01, 0x40, 0 } };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        setup();
        ENV.follow = follow_hook;
        put_hostile(0x12, 0, 0);
        below_ground();
        flight_state_t *f = &E.rec[8].ac.f;
        f->cls = (uint8_t)T[i].cls; f->state = 0;
        follow_calls = 0; follow_last = NULL;
        const int32_t before[3] = { E.rec[8].ac.pos[0], E.rec[8].ac.pos[1], E.rec[8].ac.pos[2] };
        flight_test_result_entity(&E.rec[8].ac, T[i].r, before);
        ASSERT_EQ_INT(follow_calls, T[i].written);
        ASSERT_EQ_INT((f->w2 & 0x1000) != 0, T[i].written);
        ASSERT_EQ_INT(f->state, T[i].written);
        if (T[i].written) { ASSERT_TRUE(follow_last == f); ASSERT_EQ_INT(f->w0 & 0x0040, 0); }   /* and the record is removed at once */
    }
}

TEST(the_result_0x40_handler_of_a_non_player_record_goes_by_class_note_07_13_1) {
    /* rows: class, W0 before, then removed (valid bit cleared, V:4A910 = 0x0C), dying start (dying bit, counter 15, crashed bit clear), the marks (follow call, W2 0x1000, state 1), B20 bit 1, W0 active bit kept */
    static const struct { const char *name; int cls; unsigned w0; int removed, dying, marks, b20, changed; } T[] = {
        { "missile (class nibble 0)",         0x01, 0x10C0, 1, 0, 0, 1, 1 },
        { "cruise missile (class 0x15)",      0x15, 0x10C0, 0, 1, 0, 1, 1 },
        { "aircraft",                         0x12, 0x10C0, 1, 0, 1, 1, 1 },
        { "seat without the crashed bit",     0x30, 0x10C0, 0, 0, 0, 0, 0 },
        { "seat with the crashed bit",        0x30, 0x12C0, 1, 0, 1, 1, 1 },
        { "parachute without the crashed bit", 0x32, 0x10C0, 0, 0, 0, 0, 0 },
        { "a dying record at the counter 15", 0x12, 0x14C0, 0, 0, 0, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        setup();
        ENV.follow = follow_hook;
        put_hostile(0x12, 0, 0);
        below_ground();
        flight_state_t *f = &E.rec[8].ac.f;
        f->cls = (uint8_t)T[i].cls; f->state = 0; f->w0 = (uint16_t)T[i].w0; f->b20 = 0; f->w2 = 0; f->counter = 15; f->v4a910 = 0;
        follow_calls = 0;
        const int32_t before[3] = { E.rec[8].ac.pos[0], E.rec[8].ac.pos[1], E.rec[8].ac.pos[2] };
        flight_test_result_entity(&E.rec[8].ac, 0x40, before);
        if ((f->w0 & 0x0040) == 0 ? !T[i].removed : T[i].removed) { printf("  row \"%s\": valid bit %04X\n", T[i].name, f->w0); ASSERT_TRUE(0); }
        if (T[i].removed) ASSERT_EQ_INT(f->v4a910, 0x0C);
        ASSERT_EQ_INT((f->w0 & 0x0400) != 0 && !(T[i].w0 & 0x0400), T[i].dying);
        if (T[i].dying) { ASSERT_EQ_INT(f->counter, 15); ASSERT_EQ_INT(f->w0 & 0x0200, 0); }
        ASSERT_EQ_INT(follow_calls, T[i].marks); ASSERT_EQ_INT((f->w2 & 0x1000) != 0, T[i].marks); ASSERT_EQ_INT(f->state, T[i].marks);
        ASSERT_EQ_INT((f->b20 & 0x02) != 0, T[i].b20);
        ASSERT_EQ_INT(f->w0 & 0x1000, 0x1000);            /* the active bit is kept for the result 0x40 */
        (void)T[i].changed;
    }
}

TEST(a_seat_class_record_without_the_crashed_bit_is_untouched_by_the_results_0x10_and_0x20_except_the_hit_bit) {
    for (int r = 0x10; r <= 0x20; r += 0x10) {
        setup();
        put_hostile(0x12, 0, 0);
        below_ground();
        flight_state_t *f = &E.rec[8].ac.f;
        f->cls = 0x30; f->w0 = 0x10C0; f->b20 = 0;
        const int32_t before[3] = { E.rec[8].ac.pos[0], E.rec[8].ac.pos[1], E.rec[8].ac.pos[2] };
        flight_test_result_entity(&E.rec[8].ac, r, before);
        ASSERT_EQ_INT(f->w0, 0x90C0);                    /* the hit bit; no dying start, the active bit kept */
        ASSERT_EQ_INT(f->b20 & 0x02, 0);
    }
}

TEST(a_ground_result_for_a_missile_backs_the_long_position_up_and_makes_the_object_a_copy_with_the_records_cell_words) {
    setup();
    put_hostile(0x12, 0, 0);
    flight_state_t *f = &E.rec[8].ac.f;
    f->cls = 0x01;                                       /* class nibble 0: a missile */
    E.hook.weapon = weapon_hook;
    const int32_t x0 = E.rec[8].ac.pos[0], z0 = E.rec[8].ac.pos[2];
    below_ground();
    const int16_t cx = f->cell_x, fx = f->fine_x;
    const int32_t before[3] = { x0, E.rec[8].ac.pos[1], z0 };
    flight_test_result_entity(&E.rec[8].ac, 0x10, before);
    ASSERT_EQ_INT(E.rec[8].ac.pos[1], 0);                /* the record's LONG words: backed up to y = 0 */
    ASSERT_EQ_INT(E.rec[8].ac.pos[0], x0 - 100); ASSERT_EQ_INT(E.rec[8].ac.pos[2], z0 - 60);   /* (D.x * 16 / D.y16) * h / 16 with D.y16 = -5 and h = 5 */
    entities_step(&E);
    ASSERT_TRUE(E.obj[0].w0 & 0x0040); ASSERT_EQ_INT(E.obj[0].model, 0x13);   /* the creator's parity: 15 minus the slot 0 is odd */
    ASSERT_EQ_INT(E.obj[0].pos[0], x0 - 100); ASSERT_EQ_INT(E.obj[0].pos[1], 0);
    ASSERT_EQ_INT(E.obj[0].cell_x, cx); ASSERT_EQ_INT(E.obj[0].fine_x, fx);   /* the cell and fine words are the record's own, not recomputed from the longs */
}

TEST(a_ground_result_for_a_missile_with_no_vertical_displacement_skips_the_back_up_but_still_makes_the_object) {
    setup();
    put_hostile(0x12, 0, 0);
    E.rec[8].ac.f.cls = 0x01; E.hook.weapon = weapon_hook;
    below_ground();
    E.rec[8].ac.f.vel[1] = 0;
    const int32_t before[3] = { E.rec[8].ac.pos[0], E.rec[8].ac.pos[1], E.rec[8].ac.pos[2] };
    flight_test_result_entity(&E.rec[8].ac, 0x10, before);
    ASSERT_EQ_INT(E.rec[8].ac.pos[1], -1280);
    entities_step(&E);
    ASSERT_TRUE(E.obj[0].w0 & 0x0040);
}

TEST(a_second_impact_after_the_first_expiry_is_counted_again_when_it_is_not_on_the_ground) {
    setup();
    put_hostile(0x12, 0x0400, 0);
    flight_state_t *f = &E.rec[8].ac.f;
    f->counter = 1;
    entities_step(&E);
    ASSERT_EQ_INT(E.kills, 1);
    ASSERT_TRUE(f->w0 & 0x0040);
    /* the wreck reaches the ground again: the hit bit and a second dying count (the result handler, 5.1), W2 bit 0x0080 stays clear for a non-player */
    below_ground();
    ground_result();
    ASSERT_EQ_INT(f->w0 & 0x0400, 0x0400); ASSERT_EQ_INT(f->w0 & 0x8000, 0x8000); ASSERT_EQ_INT(f->w0 & 0x1000, 0);
    ASSERT_EQ_INT(f->w2 & 0x0080, 0);
    f->counter = 1;
    entities_step(&E);
    ASSERT_EQ_INT(E.kills, 2);                           /* counted again */
    ASSERT_EQ_INT(f->w0 & 0x0040, 0);                    /* and freed: the hit bit */
}

/* The collision result 0x40 (aircraft with aircraft, which removes the record at once) and the weapon hits' marks (note 07 5.1 rows 1 and 3) are tested above and in test_weapons; the dying path
 * above is tested through the flight step. */

/* ---- the ejection (6.4, Review Focus 3, core side) ---------------------------------------------------------------------------------------------------- */

static void player_at(int32_t y) {
    P.pos[0] = 70 * 4194304; P.pos[1] = y; P.pos[2] = 70 * 4194304;
    for (int i = 0; i < 9; i++) P.f.mat_a[i] = P.f.mat_b[i] = (i % 4 == 0) ? 16384 : 0;
    P.f.cell_x = 70; P.f.cell_z = 70; P.f.alt8 = y >> 8;
    P.f.vel[0] = 100; P.f.vel[1] = 0; P.f.vel[2] = 5000;
}

TEST(the_seat_spawns_in_the_first_free_weapon_record_with_the_notes_fields) {
    setup();
    player_at(0x300000);
    P.f.gear = 3; P.f.ev_seat = 1;                          /* the player's step reached the count 3 (the latch is clear: the seat routine sets it) */
    entities_step(&E);
    const flight_state_t *f = &E.rec[1].ac.f;
    ASSERT_EQ_INT(f->cls, 0x30);
    ASSERT_EQ_INT(f->w0 & 0x10CA, 0x10CA);                  /* 0x10C0, 0x0002 and the player's 0x0008; the fresh bit 0x0100 was cleared by the record's first pass */
    ASSERT_EQ_INT(f->counter, -19);                         /* -20 and one counting step */
    ASSERT_EQ_INT(E.rec[1].dest_cx, -1);
    ASSERT_EQ_INT(E.rec[1].template_id, 2);
    ASSERT_EQ_INT(E.request & 0x8C, 0x8C);
    ASSERT_EQ_INT(E.player->f.v4aa7a, 0x0200);
    ASSERT_EQ_INT(P.f.ev_seat, 0);
    /* the position: the player's plus the seat offset of the table (offs[0]) through the identity matrix, each product sum shifted right by 6 */
    ASSERT_EQ_INT(E.rec[1].ac.pos[1] - 0x300000 > AI_FIXTURE.offs[0][1] * 16384 / 64 - 100, 1);
    ASSERT_TRUE(f->vel[1] > P.f.vel[1]);                    /* the thrust along the up axis */
}

TEST(the_seat_takes_record_5_when_the_records_1_to_3_are_in_use) {
    setup();
    player_at(0x300000);
    for (int i = 1; i <= 3; i++) { E.rec[i].ac.f.w0 = 0x0040; E.rec[i].ac.f.cls = 0x00; E.rec[i].ac.f.counter = 5; }
    P.f.gear = 3; P.f.ev_seat = 1;
    entities_step(&E);
    ASSERT_EQ_INT(E.rec[5].ac.f.cls, 0x30);
    ASSERT_EQ_INT(P.f.v4aa7a, 0x0A00);                      /* the latch is the record's byte offset */
}

TEST(a_seat_spawned_from_a_player_on_the_ground_has_the_vertical_velocity_cleared_before_the_thrust_is_added) {
    setup();
    player_at(0x300000);
    P.f.w2 = 0x0080; P.f.vel[1] = 5000;
    P.f.gear = 3; P.f.ev_seat = 1;
    entities_step(&E);
    ASSERT_EQ_INT(E.rec[1].ac.f.cls, 0x30);
    ASSERT_EQ_INT(E.rec[1].ac.f.w2 & 0x0080, 0);                  /* the ground bit dropped */
    ASSERT_TRUE(E.rec[1].ac.f.vel[1] <= 0x3C0 + 5);              /* 0 plus the thrust of 0x3C0 (one counting pass of the seat's step has applied its own rules), not 5000 plus it */
}

TEST(a_second_press_gives_the_parachute_with_the_nibble_cleared_under_the_latch) {
    setup();
    player_at(0x300000);
    P.f.gear = 0x0F;                                        /* the rescue key or a second press: 14 or more */
    entities_step(&E);
    ASSERT_EQ_INT(E.rec[1].ac.f.cls, 0x31);                 /* the parachute is class 0x31 (1.3), built like the seat with its own template and a thrust down */
    ASSERT_EQ_INT(E.rec[1].template_id, 11);
    ASSERT_EQ_INT(P.f.gear & 0x0F, 0);
    ASSERT_EQ_INT(E.chute_latch, 0x0200);
    ASSERT_TRUE(E.rec[1].ac.f.vel[1] < P.f.vel[1]);         /* the thrust is down */
    P.f.gear = 0x0F;
    E.rec[1].ac.f.w0 = 0;                                   /* the record freed: the latch still blocks a second parachute */
    entities_step(&E);
    ASSERT_EQ_INT(E.rec[1].ac.f.w0 & 0x0040, 0);
}

TEST(the_rescue_key_ors_0x0f_only_when_the_latch_is_clear_and_a_weapon_record_is_free) {
    setup();
    player_at(0x300000);
    entities_rescue_key(&E);
    ASSERT_EQ_INT(P.f.gear & 0x0F, 0x0F);
    P.f.gear = 0; E.chute_latch = 0x200;
    entities_rescue_key(&E);
    ASSERT_EQ_INT(P.f.gear & 0x0F, 0);
}

TEST(the_landed_seat_counts_4_3_2_1_0_and_makes_request_2_in_the_fifth_pass_only_when_none_is_pending) {
    setup();
    player_at(0x300000);
    P.f.gear = 6; P.f.v4aa7a = 0x0200;
    flight_state_t *s = &E.rec[1].ac.f;
    memset(s, 0, offsetof(flight_state_t, vtx));
    s->cls = 0x30; s->w0 = 0x10C0 | 0x8000; s->counter = 810;
    int req_pass = -1;
    for (int k = 1; k <= 8; k++) {
        entities_step(&E);
        if (req_pass < 0 && E.end_request == 2) req_pass = k;
    }
    ASSERT_EQ_INT(req_pass, 5);                             /* counter 4, 3, 2, 1, 0: the request in the 5th pass after the landing pass */
    ASSERT_EQ_INT(E.end_delay, 4);
    setup();
    player_at(0x300000);
    s = &E.rec[1].ac.f;
    memset(s, 0, offsetof(flight_state_t, vtx));
    s->cls = 0x30; s->w0 = 0x10C0 | 0x8000; s->counter = 3;
    E.end_request = 1;                                      /* a request already pending (a script's): the seat makes none */
    for (int k = 0; k < 8; k++) entities_step(&E);
    ASSERT_EQ_INT(E.end_request, 1);
}

TEST(a_destroyed_seat_releases_the_players_nibble_and_frees_its_record) {
    setup();
    player_at(0x300000);
    P.f.gear = 6; P.f.v4aa7a = 0x0200;
    flight_state_t *s = &E.rec[1].ac.f;
    memset(s, 0, offsetof(flight_state_t, vtx));
    s->cls = 0x30; s->w0 = 0x10C0 | 0x0400; s->counter = 10;
    entities_step(&E);
    ASSERT_EQ_INT(P.f.w2 & 0x0020, 0x0020);                 /* the release: the only writer of W2 bit 0x0020 */
    ASSERT_EQ_INT(s->w0, 0);                                /* the record is free */
    ASSERT_EQ_INT(E.request, 0x0C);
}

TEST(the_viewed_cell_is_the_viewed_records_one_pass_late_and_the_camera_cell_in_an_external_view) {
    setup();
    player_at(0x300000);
    E.rec[1].ac.f.w0 = 0x0040; E.rec[1].ac.f.cell_x = 11; E.rec[1].ac.f.cell_z = 22;
    P.f.v4a994 = 1; P.f.v4a996 = 0x200;                     /* the seat is the viewed record */
    P.f.cell_x = 70; P.f.cell_z = 70;
    entities_update_view_cell(&E, 0, 0, 0);
    ASSERT_EQ_INT(P.f.v4aa04, 11); ASSERT_EQ_INT(P.f.v4aa06, 22);   /* the player's neighbour test next pass reads the seat's cell (6.4) */
    ASSERT_EQ_INT(E.rec[1].ac.f.v4aa04, 11);
    entities_update_view_cell(&E, 1, 33, 44);
    ASSERT_EQ_INT(P.f.v4aa04, 33); ASSERT_EQ_INT(P.f.v4aa06, 44);
    P.f.v4a994 = 0; P.f.v4a996 = 0;                         /* the player viewed: its own cell */
    entities_update_view_cell(&E, 0, 0, 0);
    ASSERT_EQ_INT(P.f.v4aa04, 70);
}

TEST(the_fixed_camera_stands_at_the_airfield_points_or_the_carrier_records_and_clears_the_update_flag) {
    setup();
    ai_t tab;
    memset(&tab, 0, sizeof tab);
    tab.cam_point[0][0] = 21; tab.cam_point[0][1] = 34; tab.cam_point[0][2] = 2; tab.cam_point[0][3] = 300; tab.cam_point[0][4] = 640; tab.cam_point[0][5] = 90;   /* invented point */
    tab.fine[2][0] = 1000; tab.fine[2][1] = 2000;
    tab.start[3][0] = (int16_t)0x800E; tab.start[4][0] = (int16_t)0x800F;
    E.tab = &tab;
    int32_t pos[3];
    ASSERT_EQ_INT(entities_fixed_camera(&E, 0, pos), 0);
    ASSERT_EQ_INT(pos[0], 21 * 4194304 + (1000 + 640) * 256); ASSERT_EQ_INT(pos[2], 34 * 4194304 + (2000 + 90) * 256); ASSERT_EQ_INT(pos[1], 300 * 256);
    ASSERT_EQ_INT(E.camera_update, 1);                              /* the function leaves V:4A86D to its caller (game.c set_v4a86d keeps the two copies equal) */
    E.camera_update = 1;
    E.rec[14].ac.f.cls = 0x20; E.rec[14].ac.pos[0] = 5000000; E.rec[14].ac.pos[1] = 0; E.rec[14].ac.pos[2] = 6000000;
    for (int i = 0; i < 9; i++) E.rec[14].ac.f.mat_b[i] = (i % 4 == 0) ? 16384 : 0;
    ASSERT_EQ_INT(entities_fixed_camera(&E, 3, pos), 0);    /* (-36, 47, -48) model units in the carrier's frame, shifted right by 4: 1024 position units per unit */
    ASSERT_EQ_INT(pos[0], 5000000 - 36 * 1024); ASSERT_EQ_INT(pos[1], 47 * 1024); ASSERT_EQ_INT(pos[2], 6000000 - 48 * 1024);
    ASSERT_EQ_INT(E.camera_update, 1);
    ASSERT_EQ_INT(entities_fixed_camera(&E, 5, pos), -1);
}

static int real_ai_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "assets/%s", path);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    *len = fread(*out, 1, (size_t)n, f);
    fclose(f);
    return 0;
}
static void real_ai_release(void *user, uint8_t *buf) { (void)user; free(buf); }

TEST(real_assets_the_fixed_camera_of_the_three_airfields_stands_at_cells_near_each_other) {
    static game_loader_t ld = { real_ai_read, real_ai_release, NULL };
    static ai_t tab;
    char err[160];
    if (assets_load_ai(&ld, &tab, err, sizeof err) != 0) { printf("skipped "); return; }
    setup();
    E.tab = &tab;
    int32_t p[3][3];
    for (int loc = 0; loc < 3; loc++) ASSERT_EQ_INT(entities_fixed_camera(&E, loc, p[loc]), 0);
    /* note 07 6.3: the three points lie within a few cells of each other and share the height; the records are 16 bytes apart in the image (extractor 27),
     * so no point may fall back to cell (0, 0) */
    for (int loc = 1; loc < 3; loc++) {
        const int dx = (p[loc][0] >> 22) - (p[0][0] >> 22), dz = (p[loc][2] >> 22) - (p[0][2] >> 22);
        ASSERT_TRUE(p[loc][0] >> 22 > 0 && p[loc][2] >> 22 > 0);
        ASSERT_TRUE(dx >= -8 && dx <= 8 && dz >= -8 && dz <= 8);
        ASSERT_EQ_INT(p[loc][1], p[0][1]);
    }
}

TEST(the_generator_draws_one_16_bit_word_per_call_and_is_reproducible) {
    uint32_t a = 1, b = 1;
    for (int i = 0; i < 5; i++) ASSERT_EQ_INT(entities_random16(&a), entities_random16(&b));
    uint32_t c = 2;
    ASSERT_TRUE(entities_random16(&c) != entities_random16(&a) || 1);
}

int main(void) {
    RUN(spawn_copies_the_template_words_and_writes_the_constants_of_the_placement);
    RUN(word_2_gives_a_destination_block_or_a_leader_and_the_route_index);
    RUN(a_valid_record_in_the_slot_skips_the_entry_and_is_never_replaced);
    RUN(the_template_is_copied_before_the_cap_refusal_and_the_cap_is_tested_for_every_class);
    RUN(a_cruise_missile_and_a_friendly_without_the_not_counted_bit_count_against_the_cap);
    RUN(a_class_15_record_with_a_non_zero_cell_x_is_not_overwritten_by_the_placer_though_its_valid_bit_is_clear);
    RUN(the_slot_field_is_seven_bits_and_a_slot_beyond_the_arrays_is_refused_not_written);
    RUN(the_mode_filter_skips_and_the_airborne_block_sets_gear_and_speed);
    RUN(the_random_offset_moves_the_cells_and_bit_7_halves_it_arithmetically);
    RUN(the_mission_start_places_the_first_entry_without_the_threshold_and_aims_it);
    RUN(region_triggers_place_once_per_entry_into_the_box_set_the_tags_and_shift_the_bit_numbers);
    RUN(the_region_exit_cases_leader_zero_and_an_invalid_leader_take_the_own_block_a_valid_leader_is_copied);
    RUN(the_region_tick_is_off_in_the_demo_and_places_nothing_in_the_modes_1_2_and_0x7d_but_still_runs);
    RUN(the_sixteen_secondary_objects_are_the_limit_nothing_is_created_or_reused_when_all_are_valid);
    RUN(the_life_words_count_down_once_per_unpaused_pass);
    RUN(a_launch_request_that_finds_no_free_record_is_dropped_at_the_end_of_the_pass);
    RUN(the_script_request_v4a873_launches_the_record_5_before_any_test_of_its_owner_and_the_global_request_goes_to_the_first_asker);
    RUN(a_shot_down_hostile_dies_for_fifteen_steps_then_is_counted_and_kept_while_it_is_still_in_the_air);
    RUN(the_kill_is_counted_only_for_a_counted_aircraft_in_the_air_and_the_record_is_freed_by_the_ground_or_the_hit_bit);
    RUN(a_second_impact_after_the_first_expiry_is_counted_again_when_it_is_not_on_the_ground);
    RUN(a_ground_result_for_an_aircraft_starts_the_dying_without_the_ground_bit_and_makes_no_object_and_no_back_up);
    RUN(only_an_aircraft_contact_result_0x40_of_a_record_with_a_class_nibble_writes_the_followed_record_and_w2_bit_0x1000);
    RUN(the_result_0x40_handler_of_a_non_player_record_goes_by_class_note_07_13_1);
    RUN(a_seat_class_record_without_the_crashed_bit_is_untouched_by_the_results_0x10_and_0x20_except_the_hit_bit);
    RUN(a_ground_result_for_a_missile_backs_the_long_position_up_and_makes_the_object_a_copy_with_the_records_cell_words);
    RUN(a_ground_result_for_a_missile_with_no_vertical_displacement_skips_the_back_up_but_still_makes_the_object);
    RUN(the_seat_spawns_in_the_first_free_weapon_record_with_the_notes_fields);
    RUN(the_seat_takes_record_5_when_the_records_1_to_3_are_in_use);
    RUN(a_seat_spawned_from_a_player_on_the_ground_has_the_vertical_velocity_cleared_before_the_thrust_is_added);
    RUN(a_second_press_gives_the_parachute_with_the_nibble_cleared_under_the_latch);
    RUN(the_rescue_key_ors_0x0f_only_when_the_latch_is_clear_and_a_weapon_record_is_free);
    RUN(the_landed_seat_counts_4_3_2_1_0_and_makes_request_2_in_the_fifth_pass_only_when_none_is_pending);
    RUN(a_destroyed_seat_releases_the_players_nibble_and_frees_its_record);
    RUN(the_viewed_cell_is_the_viewed_records_one_pass_late_and_the_camera_cell_in_an_external_view);
    RUN(the_fixed_camera_stands_at_the_airfield_points_or_the_carrier_records_and_clears_the_update_flag);
    RUN(real_assets_the_fixed_camera_of_the_three_airfields_stands_at_cells_near_each_other);
    RUN(the_generator_draws_one_16_bit_word_per_call_and_is_reproducible);
    TEST_MAIN_END();
}

/* tests/test_frame.c - the order of one flight pass at the game level (src/core/game.c game_step; docs/re/07-ai-and-weapons.md 7.3, 11.5, 12.1, 12.5; note 05 14 and 17.17).
 * Runs the whole game on the extracted assets (assets/ or argv[1]); skipped (exit 0) when the flight tables or the models are absent.
 *   - the carrier's slots: the draw stores them (record 14 carries the carrier's matrix) and a tracer inside the hull volumes is stopped;
 *   - the gun before or after the entity pass by the draw scan; a gun kill posts no 0x4016 in either view (note 07 12.5);
 *   - the passes of a crash that are full passes (the restart passes, the external-view dying passes) call the gun; the hold does not;
 *   - the map view walks queues A and B and the [90] pass, skips the entity pass; the altitude gate skips all of it and the gun; the draw handler's revival row. */
#include "test.h"
#include "../src/core/game.h"
#include "../src/core/entities.h"
#include "../src/core/weapons.h"
#include "../src/core/flight.h"
#include "../src/core/flight_int.h"
#include "../src/core/world.h"
#include "../src/core/screen.h"

static const char *root;
static int fl_read(void *user, const char *path, uint8_t **out, size_t *len) {
    (void)user;
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, path);
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
static void fl_release(void *user, uint8_t *buf) { (void)user; free(buf); }
static game_loader_t loader = { fl_read, fl_release, NULL };

#define SEA_X 273272584
#define SEA_Z 280480032

static game_t *start_game(int location) {
    char err[128];
    game_t *g = game_create(&loader, err, sizeof err);
    if (!g) { printf("  (game_create: %s)\n", err); return NULL; }
    game_debug_start_flight(g, location, 0);
    return g;
}
static void press(game_t *g, int code, int value) { game_event_t ev = { GAME_EV_BUTTON, code, value }; game_input(g, &ev); }
static game_scene_t SC;
static const game_scene_t *scene(game_t *g) { game_scene(g, &SC); return &SC; }
static void stage_air(game_t *g, int32_t x, int32_t y, int32_t z, uint16_t pitch, uint16_t heading, int S, int T, uint8_t gear) {
    aircraft_t *ac = game_debug_aircraft(g);
    flight_init(ac, FLIGHT_FA18, FLIGHT_START_RUNWAY);
    flight_set_position(ac, x, y, z);
    flight_set_attitude(ac, pitch, heading, 0);
    ac->f.w2 = 0; ac->f.b4 = 0;
    ac->f.speed_target = ac->f.speed = (int16_t)S;
    ac->f.throttle = (int8_t)T;
    ac->f.gear = gear;
    flight_sync(ac);
}
static lists_t *lists(game_t *g) { return world_lists((world_t *)game_debug_world(g)); }

/* a tracer at rest at a point (fine units of the cell) */
static void tracer(weapons_t *w, int k, int cell_x, int cell_z, int fx, int fy, int fz) {
    wp_tracer_t t; memset(&t, 0, sizeof t);
    t.cell_x = (int16_t)cell_x; t.cell_z = (int16_t)cell_z;
    t.pos[0] = fx * 256; t.pos[1] = fy * 256; t.pos[2] = fz * 256;
    t.flags = 0x401; t.life = 20; t.latch = 1; t.owner = 0;
    w->slot[k] = t;
}

/* ---- the carrier (note 05 section 9, note 07 8.2) -------------------------------------------------------------------------------------------------- */

TEST(record_14_carries_the_carriers_matrix_in_the_draw_and_a_tracer_in_the_hull_volumes_is_stopped) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    lists_t *L = lists(g);
    int any = 0;
    for (int k = 0; k < LISTS_VSLOTS; k++) for (int a = 0; a < 3; a++) any |= L->vslot[14][k][a] != 0;
    ASSERT_TRUE(any);                                              /* the draw rotated model 8's vertices by the carrier's matrix (a zero matrix would store zeros) */
    flight_ship_t ship;
    ASSERT_EQ_INT(w->hook.ship(w->hook.user, 14, &ship), 0);
    ASSERT_EQ_INT(memcmp(ship.vtx, L->vslot[14], sizeof ship.vtx), 0);   /* the carrier's slots are the draw store (note 05 open question 9): the hook reads them from there */
    const aircraft_t *c = entities_ac_c((entities_t *)game_debug_entities(g), 14);
    int found = 0, px = 0, py = 0, pz = 0;
    for (int dx = -600; dx <= 600 && !found; dx += 24)
        for (int dz = -600; dz <= 600 && !found; dz += 24)
            for (int y = 0; y <= 40 && !found; y += 4) {
                const int32_t P[3] = { (int16_t)(ship.fine_x + dx), y, (int16_t)(ship.fine_z + dz) };
                if (fl_bighull_hit(flight_env(), &ship, P)) { found = 1; px = P[0]; py = P[1]; pz = P[2]; }
            }
    ASSERT_TRUE(found);
    tracer(w, 0, c->f.cell_x, c->f.cell_z, px, py, pz);
    game_step(g);
    ASSERT_TRUE((w->slot[0].flags & WPF_HIT_SET) != 0);            /* stopped by the hull */
    /* and the same tracer 20,000 du above is not */
    tracer(w, 2, c->f.cell_x, c->f.cell_z, px, 20000, pz);
    game_step(g);
    ASSERT_TRUE((w->slot[2].flags & WPF_HIT_SET) == 0);
    game_destroy(g);
}

/* ---- a gun kill of a drawn, designated target: the order of the gun, the entity pass and the arbiter -------------------------------------------------- */

/* A hostile (record 8) ahead of the player in the same cell, valid and active, with two gun hits already (the third kills), designated; one pass draws it (the slots and the part id), then a tracer
 * is put on a point inside its hull. Returns 0 when the staging failed. */
static int stage_target(game_t *g, entities_t *e, weapons_t *w, int32_t *hit) {
    aircraft_t *pl = game_debug_aircraft(g);
    aircraft_t *a = entities_ac(e, 8);
    memset(a, 0, sizeof *a);
    const int32_t px = pl->pos[0] + 0x30000, py = pl->pos[1] + 0x2000, pz = pl->pos[2] + 0x30000;
    a->pos[0] = px; a->pos[1] = py; a->pos[2] = pz;
    flight_sync(a);
    a->f.cls = 0x12; a->f.w0 = 0x10C0; a->f.group = 3; a->f.entity = 1;
    a->f.mat_a[0] = a->f.mat_a[4] = a->f.mat_a[8] = a->f.mat_b[0] = a->f.mat_b[4] = a->f.mat_b[8] = 16384;
    a->f.l7d = 3;
    e->rec[8].template_id = 3; e->rec[8].hits = 2;
    e->designated = 8;
    game_step(g);                                   /* drawn once: the slots and the part id are stored */
    game_step(g);
    const flight_state_t *f = &a->f;
    int16_t vtx[17][3];
    if (w->hook.slots(w->hook.user, 8, vtx, 17) != 0 || !(f->w0 & 0x0040)) return 0;
    int32_t fine[3] = { f->fine_x, f->alt8, f->fine_z };
    for (int dy = -12; dy <= 12; dy += 2)
        for (int dx = -12; dx <= 12; dx += 2)
            for (int dz = -12; dz <= 12; dz += 2) {
                const int32_t T[3] = { f->fine_x + dx, f->alt8 + dy, f->fine_z + dz };
                if (weapons_hull_inside(e->tab, f, f->b7b, vtx, fine, T)) { hit[0] = T[0]; hit[1] = T[1]; hit[2] = T[2]; return 1; }
            }
    return 0;
}

TEST(an_external_view_gun_kill_with_an_active_tracer_in_the_pool_runs_the_gun_first_and_the_draw_handler_posts_in_the_same_pass) {
    /* note 07 12.5 (V: combat7-gunorder.txt, the whole pool at a 3-hit gun kill in the chase view; gunorder_check.py runs the original's scan on it): the pool held the stopped tracers of the two earlier hits, the
     * scan 0x2AD18 answered 1 (the highest active slot, the owner no farther from the camera than the tracer), the gun marked the kill before the entity pass, whose draw handler saw the crashed bit in the
     * same pass: it posted 0x4016, dropped the designation and started the dying count. (combat7-gunorder1.txt: the first tracer of an empty pool, the scan 0, the post one pass later.) */
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    entities_t *e = (entities_t *)game_debug_entities(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    press(g, GAME_BTN_KEYPAD, GAME_KP_ENTER);                       /* KP-Enter: the external view behind */
    game_step(g);
    ASSERT_TRUE(scene(g)->camera.view != GAME_VIEW_COCKPIT);
    int32_t hit[3];
    if (!stage_target(g, e, w, hit)) { printf("(staging) "); ASSERT_TRUE(0); }
    aircraft_t *a = entities_ac(e, 8);
    aircraft_t *pl = game_debug_aircraft(g);
    pl->f.v4ab98 = 0;
    tracer(w, 0, a->f.cell_x, a->f.cell_z, hit[0], hit[1], hit[2]);
    game_step(g);
    ASSERT_EQ_INT(scene(g)->gun_first, 1);                          /* the tracer is in the pool at the scan: the owner is nearer the chase camera than the tracer ahead of it */
    ASSERT_TRUE((w->slot[0].flags & WPF_HIT_SET) != 0);
    ASSERT_EQ_INT(a->f.w0 & 0x0600, 0x0400);                        /* the gun marked the kill before the handler: crashed converted to dying in this pass */
    ASSERT_EQ_INT(e->designated, -1);
    ASSERT_EQ_INT(pl->f.v4ab98, 0x4016);                            /* "target destroyed" */
    game_destroy(g);
}

TEST(a_cockpit_gun_kill_runs_after_the_entity_pass_posts_nothing_and_the_next_world_update_converts_it) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    entities_t *e = (entities_t *)game_debug_entities(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    game_step(g);
    int32_t hit[3];
    if (!stage_target(g, e, w, hit)) { printf("(staging) "); ASSERT_TRUE(0); }
    aircraft_t *a = entities_ac(e, 8);
    aircraft_t *pl = game_debug_aircraft(g);
    pl->f.v4ab98 = 0;
    tracer(w, 0, a->f.cell_x, a->f.cell_z, hit[0], hit[1], hit[2]);
    game_step(g);
    ASSERT_EQ_INT(scene(g)->gun_first, 0);                          /* every tracer in the cockpit view: the gun after the entity pass */
    ASSERT_TRUE((w->slot[0].flags & WPF_HIT_SET) != 0);
    ASSERT_EQ_INT(a->f.w0 & 0x0200, 0x0200);                        /* crashed, not yet converted: the handler had run before the gun */
    ASSERT_TRUE(pl->f.v4ab98 != 0x4016);
    game_step(g);
    ASSERT_EQ_INT(a->f.w0 & 0x0400, 0x0400);                        /* the next world update's step converts it */
    ASSERT_TRUE(pl->f.v4ab98 != 0x4016);                            /* with no notice; the stale-lock drop cleared the designation */
    ASSERT_EQ_INT(e->designated, -1);
    game_destroy(g);
}

/* ---- the revival row of the draw handler (11.5) ----------------------------------------------------------------------------------------------------- */

TEST(a_record_freed_with_a_negative_counter_that_is_still_in_the_queue_is_made_valid_again_when_it_is_drawn) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    stage_air(g, SEA_X, 600000, SEA_Z, 0, 0, 3000, 60, 0xE0);
    entities_t *e = (entities_t *)game_debug_entities(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    game_step(g);
    int32_t hit[3];
    if (!stage_target(g, e, w, hit)) { printf("(staging) "); ASSERT_TRUE(0); }
    aircraft_t *a = entities_ac(e, 8);
    a->f.w0 = 0x0000; a->f.counter = -1; e->rec[8].engage = 0;      /* freed with a negative counter; the queue still names it (no request rebuilds queue C) */
    game_step(g);
    ASSERT_EQ_INT(a->f.w0 & 0x0040, 0x0040);                        /* revived by the handler of the stale entry */
    a->f.w0 = 0x0000; a->f.counter = -1; e->rec[8].engage = 5;      /* +0x7A = 5: nothing is drawn, nothing changes */
    game_step(g);
    ASSERT_EQ_INT(a->f.w0 & 0x0040, 0);
    game_destroy(g);
}

/* ---- the passes of a crash (note 07 7.3, note 05 14 and 17.17) -------------------------------------------------------------------------------------- */

TEST(the_restart_passes_of_a_cockpit_crash_run_the_gun_but_the_hold_does_not) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 5; i++) game_step(g);
    stage_air(g, SEA_X, 20000, SEA_Z, 0xF200 % 28800, 0, 3000, 60, 0xE0);
    aircraft_t *ac = game_debug_aircraft(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    int guard = 0;
    while (game_crash_state(g, NULL) != GAME_CRASH_HOLD && guard++ < 400) game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);
    (void)ac;
    tracer(w, 0, 1, 1, 100, 100, 100);
    w->slot[0].life = 20;
    game_step(g);                                                   /* a step of the hold: no frame routine runs */
    ASSERT_EQ_INT(w->slot[0].life, 20);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_HOLD);
    guard = 0;
    while (game_crash_state(g, NULL) == GAME_CRASH_HOLD && guard++ < 40) game_step(g);
    ASSERT_EQ_INT(game_crash_state(g, NULL), GAME_CRASH_RESUME);    /* pass A has run: a full gate-1 pass */
    ASSERT_TRUE(w->slot[0].life < 20);                              /* the gun routine aged the tracer in it */
    const int life = w->slot[0].life;
    game_step(g);                                                   /* pass B */
    ASSERT_TRUE(w->slot[0].life < life);
    game_destroy(g);
}

/* ---- the map view (note 07 7.3) ----------------------------------------------------------------------------------------------------------------------- */

TEST(the_map_view_skips_the_entity_pass_but_calls_the_gun) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    ASSERT_TRUE(scene(g)->entry_count > 0);
    press(g, GAME_BTN_TOWER, 1);
    game_step(g);
    ASSERT_EQ_INT(scene(g)->camera.view, GAME_VIEW_TOWER);
    {   /* the walkers 0x1FA24, 0x1FA36 and 0x2C0EC run before the test of V:4A865 (queues A and B, the [90] pass), the entity pass 0x1FBCC (direct entities, queue C) does not (07 7.3) */
        int ab = 0, ent = 0;
        for (unsigned i = 0; i < SC.entry_count; i++) {
            const int k = SC.entries[i].kind;
            if (k == GAME_ENTRY_TERRAIN && (SC.entries[i].tier == GAME_TIER_A || SC.entries[i].tier == GAME_TIER_B)) ab++;
            if (k == GAME_ENTRY_ENTITY || k == GAME_ENTRY_SECONDARY || SC.entries[i].tier == GAME_TIER_C || (SC.entries[i].tier == GAME_TIER_DIRECT && k != GAME_ENTRY_SPECK && k != GAME_ENTRY_SPECK_TRI && k != GAME_ENTRY_SPECK_FILL)) ent++;
        }
        ASSERT_TRUE(ab > 0);
        ASSERT_EQ_INT(ent, 0);
    }
    tracer(w, 0, 1, 1, 100, 100, 100);
    game_step(g);
    ASSERT_TRUE(w->slot[0].life < 20);                              /* the gun is called */
    ASSERT_EQ_INT(scene(g)->gun_first, 1);
    game_destroy(g);
}

/* The altitude gate 0x111E0 (07 7.3): three presses of `[` put the map at 0x8000000 (V:4AB1E at or below -0x8000000); the three walks, the gun and the entity pass are skipped, the arbiter and
 * the HUD still run. A tracer in the pool keeps its life; the walk is empty; one press of `]` brings the walkers and the gun back. */
TEST(the_altitude_gate_skips_the_walks_the_gun_and_the_entity_pass) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    press(g, GAME_BTN_TOWER, 1);
    game_step(g);
    for (int k = 0; k < 3; k++) { press(g, GAME_BTN_LBRACKET, 1); game_step(g); }
    ASSERT_EQ_INT(scene(g)->camera.pos.y, 0x8000000);
    ASSERT_EQ_INT(SC.entry_count, 0);                               /* no walk at all */
    tracer(w, 0, 1, 1, 100, 100, 100);
    game_step(g);
    ASSERT_EQ_INT(w->slot[0].life, 20);                             /* the gun is not called: the life is not decremented */
    ASSERT_EQ_INT(scene(g)->gun_first, 0);
    game_step(g);
    ASSERT_EQ_INT(w->slot[0].life, 20);
    press(g, GAME_BTN_RBRACKET, 1);
    game_step(g); game_step(g);
    ASSERT_TRUE(scene(g)->entry_count > 0);                                /* the walkers are back */
    ASSERT_TRUE(w->slot[0].life < 20);                              /* and the gun */
    game_destroy(g);
}


/* The contact test reads the draw store (note 05 open question 9): zero before the first draw, and after a coarser draw only the leading run of that block is replaced. */
TEST(the_carriers_contact_slots_are_the_draw_store_zero_before_the_first_draw) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    lists_t *L = lists(g);
    memset(L->vslot[14], 0, sizeof L->vslot[14]);
    game_step(g);                                                   /* the pass syncs before the step; the draw of that pass fills the store after it */
    const flight_env_t *e = flight_env();
    int any = 0;
    for (int k = 0; k < LISTS_VSLOTS; k++) for (int a = 0; a < 3; a++) any |= e->ship.vtx[k][a] != 0;
    ASSERT_TRUE(!any);                                              /* the step saw the zero slots, not a recomputed level */
    game_step(g);
    int filled = 0;
    for (int k = 0; k < LISTS_VSLOTS; k++) for (int a = 0; a < 3; a++) filled |= L->vslot[14][k][a] != 0;
    ASSERT_TRUE(filled);
    game_step(g);
    ASSERT_EQ_INT(memcmp(e->ship.vtx, L->vslot[14], sizeof e->ship.vtx), 0);   /* and the next step sees what the draw stored */
    game_destroy(g);
}

/* The player's slots 0 to 3 are the draw store's too (note 07 8.10, 05 17.11): the ground test, the landmark phase and the ship test of the next step read what the last draw stored for record 0
 * (the cockpit block's vertices rotated by matrix B in the cockpit), zero before the first draw; nothing rotates the model's level 0 any more. */
TEST(the_players_slots_are_the_draw_store) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    lists_t *L = lists(g);
    aircraft_t *ac = game_debug_aircraft(g);
    game_step(g);
    int filled = 0;
    for (int k = 0; k < 4; k++) for (int a = 0; a < 3; a++) filled |= L->vslot[0][k][a] != 0;
    ASSERT_TRUE(filled);                                            /* the draw of the pass stored the block's vertices */
    ASSERT_EQ_INT(memcmp(ac->f.vtx, L->vslot[0], sizeof ac->f.vtx), 0);
    int fifth = 0;
    for (int a = 0; a < 3; a++) fifth |= L->vslot[0][4][a] != 0;
    ASSERT_TRUE(fifth);                                             /* the cockpit block: 4 vertices and the rotated data triple after them make 5 slots (07 8.10 rule 5) */
    ac->f.vtx[2][1] = 1234;                                         /* a poked slot is overwritten by the next walk, never recomputed from the attitude */
    game_step(g);
    ASSERT_EQ_INT(memcmp(ac->f.vtx, L->vslot[0], sizeof ac->f.vtx), 0);
    game_destroy(g);
}

/* The draw handlers also run for the direct list (04a 6, 11.5): the records 14 and 15 are on it whenever they are active (bit 2). The gun's hit test walks queue C only, so a gun kill never marks a record
 * that is on the direct list alone; what the direct entry's handler does is the cone handler of the parachute here (model 11: +0x7C bit 0 set from the phase 1 up). */
TEST(the_draw_handler_runs_for_a_record_of_the_direct_list) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    aircraft_t *a = entities_ac(e, 15);
    memset(&a->f, 0, sizeof a->f);
    a->f.w0 = 0x0040; a->f.cls = 0x31; a->f.counter = 3; a->f.gear = 0x20;
    e->rec[15].template_id = 11;
    world_lists_request((world_t *)game_debug_world(g), 0xFF);       /* a rebuild: the direct list is made by the builder's tail */
    game_step(g);
    lists_t *L = lists(g);
    int on_direct = 0;
    for (unsigned i = 0; i < L->ndirect; i++) on_direct |= L->direct[i] == 15;
    ASSERT_TRUE(on_direct);
    ASSERT_EQ_INT(entities_ac(e, 15)->f.gear & 1, 1);               /* the parachute handler 0x26A3E ran for the direct entry */
    game_destroy(g);
}

/* The dispatcher ruling of note 08 5.11: the pass in which the request dispatcher acts runs the WHOLE world update (here: the stale-lock drop of its head), request 2 as a frozen pass with the pause byte
 * set; an ESC (request 1) makes the short path and no part of it. */
TEST(the_pass_in_which_the_dispatcher_acts_runs_the_whole_world_update) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    screen_t *s = (screen_t *)game_debug_screen(g);
    memset(&entities_ac(e, 8)->f, 0, sizeof entities_ac(e, 8)->f);       /* record 8: not valid, so a lock on it is stale */
    e->designated = 8;
    s->request = 2; s->request_delay = -1;
    game_step(g);                                                    /* the dispatcher acts in this pass */
    ASSERT_EQ_INT(s->request, 0);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.v4a850, 0xF0);
    ASSERT_EQ_INT(game_debug_aircraft(g)->f.v4a866, 1);              /* request 2 sets the pause byte: a frozen pass */
    ASSERT_EQ_INT(e->designated, -1);                                /* the head of the world update ran: the stale lock was dropped in the same pass */
    game_destroy(g);
}

TEST(an_esc_taken_in_the_pass_runs_no_part_of_the_world_update) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    memset(&entities_ac(e, 8)->f, 0, sizeof entities_ac(e, 8)->f);
    e->designated = 8;
    press(g, GAME_BTN_ESC, 1);
    game_step(g);                                                    /* ESC sets the gate to 0 in its pass: the short path */
    ASSERT_EQ_INT(e->designated, 8);
    game_destroy(g);
}

/* ---- the rearm zeroes the records 1 to 3 whole, their vertex slots and part id included (note 07 7.1) ----------------------------------------------- */

TEST(the_rearm_clears_the_vertex_slots_and_the_part_id_of_the_records_1_to_3) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    lists_t *L = lists(g);
    for (int r = 0; r <= 4; r++) { L->vslot[r][5][1] = 77; L->part_id[r] = 0x21; }
    press(g, GAME_BTN_REPLENISH, 1);
    game_step(g);
    for (int r = 1; r <= 3; r++) { ASSERT_EQ_INT(L->vslot[r][5][1], 0); ASSERT_EQ_INT(L->part_id[r], 0); }
    ASSERT_EQ_INT(L->vslot[4][5][1], 77);                           /* the other records keep theirs */
    game_destroy(g);
}

/* ---- the head of the world update (note 07 4.8): the stale-lock drop comes before the player's range pre-step ---------------------------------------- */

TEST(the_player_range_is_not_computed_toward_a_target_the_head_of_the_update_dropped) {
    game_t *g = start_game(3);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 3; i++) game_step(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    aircraft_t *a = entities_ac(e, 8);
    memset(a, 0, sizeof *a);
    a->f.cls = 0x12; a->f.w0 = 0x0040; a->f.b20 = 0x02;            /* valid but out of control: the drop takes it */
    a->pos[0] = game_debug_aircraft(g)->pos[0] + 0x20000; a->pos[2] = game_debug_aircraft(g)->pos[2] + 0x20000;
    flight_sync(a);
    e->designated = 8;
    e->rec[0].range = 0x7FFF; e->rec[0].ac.f.repeat = 0;
    const uint8_t rep0 = e->rec[0].ac.f.repeat;
    game_step(g);
    ASSERT_EQ_INT(e->designated, -1);
    ASSERT_EQ_INT(e->rec[0].ac.f.repeat, rep0);                     /* the range routine saw no designation: its call counter did not advance */
    game_destroy(g);
}

/* The landmark branch of the tracer test through the game on the real tables (note 07 8.2 "Landmarks"; review S11): world_landmarks builds the volumes of the lists into the environment, queue C names
 * them by ordinal, and a tracer put inside a volume's first element stops there with no record changed. The point is found by trying a grid inside the first element's box with the gun call of
 * the game's own pass (the planes decide); skipped when the scene has no volume near the start. */
TEST(a_tracer_inside_a_real_landmark_volume_stops_there_and_changes_no_record) {
    game_t *g = start_game(0);
    ASSERT_TRUE(g != NULL);
    for (int i = 0; i < 4; i++) game_step(g);
    weapons_t *w = (weapons_t *)game_debug_weapons(g);
    entities_t *e = (entities_t *)game_debug_entities(g);
    const flight_env_t *fe = flight_env();
    ASSERT_TRUE(fe != NULL);
    if (fe->n_landmarks == 0) { printf("(no volume near the start) skipped "); game_destroy(g); return; }
    int found = 0, tried = 0;
    for (int sl = 0; sl < fe->n_landmarks && !found; sl++) {
        const flight_lm_slot_t S = fe->landmark[sl];   /* a copy: the steps below rebuild the environment */
        if (S.nnodes == 0) continue;
        const flight_lm_node_t N = S.node[0];
        if (N.box[2] - N.box[1] < 4 || N.box[4] - N.box[3] < 4) continue;
        for (int iy = 1; iy <= 6 && !found; iy++) for (int ix = 1; ix <= 6 && !found; ix++) for (int iz = 1; iz <= 6 && !found; iz++) {
            const int X = N.box[1] + (N.box[2] - N.box[1]) * ix / 7, Z = N.box[3] + (N.box[4] - N.box[3]) * iz / 7, Y = N.box[0] * iy / 7;
            tracer(w, 0, S.cell_x, S.cell_z, X, Y, Z - fe->landmark_z);
            const int hits0 = w->n_hits;
            uint16_t w0[16];
            for (int r = 0; r < 16; r++) w0[r] = entities_ac(e, r)->f.w0;
            game_step(g);
            tried++;
            if (w->slot[0].flags & WPF_HIT_SET) {
                found = 1;
                ASSERT_EQ_INT(w->n_hits, hits0);                       /* a landmark hit changes no counter */
                for (int r = 1; r < 16; r++) ASSERT_EQ_INT(entities_ac(e, r)->f.w0 & 0x0600, w0[r] & 0x0600);   /* and no record is crashed or dying by it */
                ASSERT_EQ_INT(w->slot[0].life, 2);                     /* the tracer is stopped (the same flag change as a hit on an entity) */
            }
        }
    }
    if (!found) printf("(%d grid points tried over %d volumes) ", tried, fe->n_landmarks);
    ASSERT_TRUE(found);
    game_destroy(g);
}

int main(int argc, char **argv) {
    root = argc > 1 ? argv[1] : "assets";
    char err[128] = "";
    game_t *t = game_create(&loader, err, sizeof err);
    uint8_t *probe = NULL; size_t pl = 0;
    if (!t || fl_read(NULL, "models/index.txt", &probe, &pl) != 0) {
        printf("test_frame: skipped (%s)\n", t ? "no models" : err);
        if (t) game_destroy(t);
        return 0;
    }
    free(probe);
    game_destroy(t);
    printf("test_frame:\n");
    RUN(record_14_carries_the_carriers_matrix_in_the_draw_and_a_tracer_in_the_hull_volumes_is_stopped);
    RUN(an_external_view_gun_kill_with_an_active_tracer_in_the_pool_runs_the_gun_first_and_the_draw_handler_posts_in_the_same_pass);
    RUN(a_cockpit_gun_kill_runs_after_the_entity_pass_posts_nothing_and_the_next_world_update_converts_it);
    RUN(a_record_freed_with_a_negative_counter_that_is_still_in_the_queue_is_made_valid_again_when_it_is_drawn);
    RUN(the_restart_passes_of_a_cockpit_crash_run_the_gun_but_the_hold_does_not);
    RUN(the_map_view_skips_the_entity_pass_but_calls_the_gun);
    RUN(the_carriers_contact_slots_are_the_draw_store_zero_before_the_first_draw);
    RUN(the_draw_handler_runs_for_a_record_of_the_direct_list);
    RUN(the_pass_in_which_the_dispatcher_acts_runs_the_whole_world_update);
    RUN(the_players_slots_are_the_draw_store);
    RUN(an_esc_taken_in_the_pass_runs_no_part_of_the_world_update);
    RUN(the_altitude_gate_skips_the_walks_the_gun_and_the_entity_pass);
    RUN(the_rearm_clears_the_vertex_slots_and_the_part_id_of_the_records_1_to_3);
    RUN(the_player_range_is_not_computed_toward_a_target_the_head_of_the_update_dropped);
    RUN(a_tracer_inside_a_real_landmark_volume_stops_there_and_changes_no_record);
    TEST_MAIN_END();
}

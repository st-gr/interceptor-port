/* tests/test_engine.c - the text engine of src/core/screen.c against the worked totals of docs/re/08-game-flow.md 3.4 (passes from the start of a list to the finished flag, on the
 * extracted text; skipped without assets/). This file includes screen.c itself to reach the engine's passes. */
#include "test.h"
#include "../src/core/screen.c"

static int rd(void *user, const char *path, uint8_t **out, size_t *len) {
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
static void rel(void *user, uint8_t *b) { (void)user; free(b); }

/* passes counted until the finished flag; also the pass that ends the list and the clicks and glyph blits up to it */
static int play(screen_t *s, uint16_t id, int *list_end, long *clicks, long *blits) {
    eng_clear(s);
    eng_list(s, &id, 1, BUDGET_STEP);
    int n = 0;
    *list_end = -1; *clicks = 0; *blits = 0;
    while (n < 100000) {
        int c, b;
        const int active = s->eng.cur < s->eng.n;
        eng_pass(s, &c, &b);
        n++;
        if (active) { *clicks += c; *blits += b; }
        if (active && s->eng.cur >= s->eng.n) *list_end = n;
        if (s->eng.mode_byte & 0x80) break;
    }
    return n;
}

TEST(the_engine_reproduces_the_worked_totals_of_note_08_3_4_on_the_extracted_text) {
    game_loader_t ld = { rd, rel, NULL };
    uint8_t *b; size_t n;
    if (rd(NULL, "tables/text.bin", &b, &n) != 0) { printf("skipped "); return; }
    free(b);
    screen_t *s = (screen_t *)calloc(1, sizeof *s);
    char err[200] = "";
    ASSERT_EQ_INT(screen_init(s, &ld, err, sizeof err), 0);
    static const struct { uint16_t id; int total; } W[] = {
        { 0x63, 375 }, { 0x62, 416 }, { 0x26, 140 }, { 0x4A, 679 }, { 0x0F, 1159 }, { 0x49, 2041 }, { 0x09, 2277 } };
    for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
        int end; long clicks, blits;
        ASSERT_EQ_INT(play(s, W[i].id, &end, &clicks, &blits), W[i].total);
        ASSERT_EQ_INT(end, W[i].total - 50);                        /* the end hold is 50 passes */
    }
    int end; long clicks, blits;
    ASSERT_EQ_INT(play(s, 0x0F, &end, &clicks, &blits), 1159);       /* the credits: 1,109 passes to the end of the list, 174 click requests, 348 glyph blits (3.8) */
    ASSERT_EQ_INT(end, 1109); ASSERT_EQ_INT(clicks, 174); ASSERT_EQ_INT(blits, 348);
    ASSERT_EQ_INT(play(s, 0x63, &end, &clicks, &blits), 375);        /* the game over: 325 passes, 41 requests, 82 blits */
    ASSERT_EQ_INT(end, 325); ASSERT_EQ_INT(clicks, 41); ASSERT_EQ_INT(blits, 82);
    screen_destroy(s); free(s);
}

int main(void) {
    RUN(the_engine_reproduces_the_worked_totals_of_note_08_3_4_on_the_extracted_text);
    TEST_MAIN_END();
}

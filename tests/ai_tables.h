/* tests/ai_tables.h - INVENTED AI tables for the unit tests: the shapes of tables/ai.bin (assets.h ai_t) with values from simple formulas, none of them the original's. The core has no built-in copy;
 * the real tables are loaded from assets/tables/ai.bin at run time by the tests that compare against the original (they skip without it).
 *   rate[t][k]      = 40 * (t + 1) * (k + 1)           (rising, distinct per table)
 *   evasion[g][p]   = { 30 + 10 * (g % 3), 1 + (g + 2 * p) % 7 }   (counter, state)
 *   offs[i]         = { (i % 5) * 3 - 6, (i % 3) + 2, 2 * i + 1 }
 *   pre_word        = 1234
 * The other sub-tables (ring3, start, cam_point, fine, runs, hull) stay zero. */
#ifndef INTERCEPTOR_TEST_AI_TABLES_H
#define INTERCEPTOR_TEST_AI_TABLES_H
#include "../src/core/assets.h"

static const ai_t *ai_fixture(void) {
    static ai_t t;
    static int ready;
    if (!ready) {
        for (int i = 0; i < AI_RATES; i++)
            for (int k = 0; k < AI_RATE_ENTRIES; k++) t.rate[i][k] = (int16_t)(40 * (i + 1) * (k + 1));
        for (int g = 0; g < AI_EVASION_GROUPS; g++)
            for (int p = 0; p < 4; p++) { t.evasion[g][p][0] = (int16_t)(30 + 10 * (g % 3)); t.evasion[g][p][1] = (int16_t)(1 + (g + 2 * p) % 7); }
        for (int i = 0; i < AI_OFFSETS; i++) {
            t.offs[i][0] = (int16_t)((i % 5) * 3 - 6); t.offs[i][1] = (int16_t)((i % 3) + 2); t.offs[i][2] = (int16_t)(2 * i + 1);
        }
        t.pre_word = 1234;
        ready = 1;
    }
    return &t;
}
#define AI_FIXTURE (*ai_fixture())

#endif

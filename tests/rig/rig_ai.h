/* tests/rig/rig_ai.h - loads the extracted tables/ai.bin for a rig harness (the rig compares against the original, so it needs the real tables; the harness skips without them). */
#ifndef INTERCEPTOR_RIG_AI_H
#define INTERCEPTOR_RIG_AI_H
#include <stdio.h>
#include <stdlib.h>
#include "../../src/core/assets.h"

static int rig_ai_read(void *user, const char *path, uint8_t **data, size_t *len) {
    (void)user;
    static const char *roots[] = { "../../assets/", "assets/" };
    for (int i = 0; i < 2; i++) {
        char p[512];
        snprintf(p, sizeof p, "%s%s", roots[i], path);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *b = (uint8_t *)malloc((size_t)n + 1);
        if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return -1; }
        fclose(f);
        *data = b; *len = (size_t)n;
        return 0;
    }
    return -1;
}
static void rig_ai_release(void *user, uint8_t *buf) { (void)user; free(buf); }

/* 0 when loaded */
static int rig_ai_load(ai_t *out) {
    game_loader_t ld = { rig_ai_read, rig_ai_release, NULL };
    char err[160];
    return assets_load_ai(&ld, out, err, sizeof err);
}
#endif

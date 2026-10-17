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
    if (!ctx) { snprintf(err, errlen, "out of memory"); return -1; }
    strncpy(ctx->dir, dir, sizeof ctx->dir - 1);
    out->read = read_file; out->release = release_file; out->user = ctx;
    return 0;
}

void assets_close(game_loader_t *loader) { free(loader->user); loader->user = NULL; }

int assets_load_menu_font(const game_loader_t *loader, uint8_t out[MENU_FONT_BYTES], char *err, size_t errlen) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (!loader || !loader->read || loader->read(loader->user, "fonts/menu5x7.bin", &buf, &len) != 0) {
        if (err && errlen) snprintf(err, errlen, "fonts/menu5x7.bin: cannot be read (re-run the extractor)");
        return -1;
    }
    int ok = len == MENU_FONT_BYTES;
    if (ok) memcpy(out, buf, MENU_FONT_BYTES);
    loader->release(loader->user, buf);
    if (!ok) {
        if (err && errlen) snprintf(err, errlen, "fonts/menu5x7.bin: %zu bytes, expected %d", len, MENU_FONT_BYTES);
        return -1;
    }
    return 0;
}

int assets_load_sample(const game_loader_t *loader, int file, int8_t **data, uint32_t *len) {
    if (data) *data = NULL;
    if (len) *len = 0;
    if (!loader || !loader->read || !data || !len || file < 0) return -1;
    uint8_t *idx = NULL;
    size_t n = 0;
    if (loader->read(loader->user, "sounds/index.txt", &idx, &n) != 0) return -1;
    char path[96] = "sounds/";
    int line = 0, ok = 0;
    size_t i = 0;
    while (i < n && !ok) {
        size_t j = i;
        while (j < n && idx[j] != '\n') j++;
        size_t e = j;
        while (e > i && (idx[e - 1] == '\r' || idx[e - 1] == ' ')) e--;
        if (line == file) {
            if (e > i && e - i < 64) {
                memcpy(path + 7, idx + i, e - i);
                memcpy(path + 7 + (e - i), ".raw", 5);
                ok = 1;
            }
            break;
        }
        line++;
        i = j + 1;
    }
    loader->release(loader->user, idx);
    if (!ok) return -1;
    uint8_t *buf = NULL;
    size_t blen = 0;
    if (loader->read(loader->user, path, &buf, &blen) != 0) return -1;
    if (blen == 0 || blen > 0xFFFFFFu) { loader->release(loader->user, buf); return -1; }
    int8_t *copy = (int8_t *)malloc(blen);
    if (copy) memcpy(copy, buf, blen);
    loader->release(loader->user, buf);
    if (!copy) return -1;
    *data = copy; *len = (uint32_t)blen;
    return 0;
}

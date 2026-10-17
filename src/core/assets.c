/* src/core/assets.c - asset index and version helpers. */
#include <stdint.h>
#include "assets.h"

int core_assets_load_index(const game_loader_t *loader, const char *path,
                           char names[][CORE_ASSET_NAME_LEN], int max) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (!loader || !loader->read || !loader->release) return -1;
    if (loader->read(loader->user, path, &buf, &len) != 0) return -1;
    int count = 0, ok = 1;
    size_t i = 0;
    while (i < len && ok) {
        size_t start = i;
        while (i < len && buf[i] != '\n') i++;
        size_t end = i;
        if (i < len) i++;
        while (end > start && buf[end - 1] == '\r') end--;
        if (end == start) continue;
        if (end - start >= CORE_ASSET_NAME_LEN || count >= max) { ok = 0; break; }
        for (size_t k = start; k < end; k++) names[count][k - start] = (char)buf[k];
        names[count][end - start] = '\0';
        count++;
    }
    loader->release(loader->user, buf);
    return (ok && count > 0) ? count : -1;
}

int core_assets_read_version(const game_loader_t *loader) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (loader->read(loader->user, "VERSION", &buf, &len) != 0) return -1;
    long v = 0;
    size_t i = 0, digits = 0;
    while (i < len && digits < 6 && buf[i] >= '0' && buf[i] <= '9') { v = v * 10 + (buf[i] - '0'); i++; digits++; }
    while (i < len && (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ')) i++;
    int ok = digits > 0 && i == len;
    loader->release(loader->user, buf);
    return ok ? (int)v : -1;
}

int core_assets_read_exact(const game_loader_t *loader, const char *path, uint8_t *out, size_t nbytes) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (!loader || !loader->read || !loader->release) return -1;
    if (loader->read(loader->user, path, &buf, &len) != 0) return -1;
    int ok = len == nbytes;
    if (ok) for (size_t i = 0; i < nbytes; i++) out[i] = buf[i];
    loader->release(loader->user, buf);
    return ok ? 0 : -1;
}

int core_assets_read_words(const game_loader_t *loader, const char *path, int16_t *out, size_t nwords) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (!loader || !loader->read || !loader->release) return -1;
    if (loader->read(loader->user, path, &buf, &len) != 0) return -1;
    int ok = len == 2 * nwords;
    if (ok) for (size_t i = 0; i < nwords; i++) out[i] = (int16_t)(uint16_t)(buf[2 * i] | (buf[2 * i + 1] << 8));
    loader->release(loader->user, buf);
    return ok ? 0 : -1;
}

void core_assets_append(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (!dst || cap == 0) return;
    while (n < cap - 1 && dst[n]) n++;
    while (n < cap - 1 && *src) dst[n++] = *src++;
    dst[n] = '\0';
}

void core_assets_append_u32(char *dst, size_t cap, uint32_t v) {
    char tmp[11];
    int i = 10;
    tmp[i] = '\0';
    do { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    core_assets_append(dst, cap, tmp + i);
}

/* src/frontend/sdl/logfile.c - see logfile.h. */
#include "logfile.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0700)
#endif

int logfile_default_path(char *out, size_t cap)
{
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    int n;
    if (x && x[0] == '/') n = snprintf(out, cap, "%s/interceptor/flightlog.bin", x);
    else if (h && h[0]) n = snprintf(out, cap, "%s/.config/interceptor/flightlog.bin", h);
    else return -1;
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

int logfile_load(const char *path, unsigned char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, cap, f);
    int rc = (int)got;
    if (ferror(f)) rc = -2;
    else if (got == cap) { int extra = fgetc(f); if (extra != EOF) rc = -3; }
    fclose(f);
    return rc;
}

/* mkdir -p of the directory part of path */
static int make_dirs(const char *path)
{
    char d[1024];
    size_t n = strlen(path);
    if (n >= sizeof d) return -1;
    memcpy(d, path, n + 1);
    char *slash = strrchr(d, '/');
    if (!slash) return 0;
    *slash = 0;
    for (char *p = d + 1; ; p++) {
        if (*p == '/' || *p == 0) {
            char c = *p;
            *p = 0;
            if (MKDIR(d) != 0 && errno != EEXIST) return -1;
            *p = c;
            if (!c) break;
        }
    }
    return 0;
}

int logfile_save(const char *path, const unsigned char *buf, size_t len)
{
    char tmp[1100];
    if (!path || snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    if (make_dirs(path) != 0) return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(buf, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); return -2; }
#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp, path) != 0) { remove(tmp); return -2; }
    return 0;
}

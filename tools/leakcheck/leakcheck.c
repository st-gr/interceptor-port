/* tools/leakcheck/leakcheck.c - LD_PRELOAD allocation tracker for leak profiling where
 * valgrind (stripped system ld.so) and LeakSanitizer (ptrace denied) cannot run.
 * Build: cc -O1 -g -shared -fPIC -o build/leakcheck.so tools/leakcheck/leakcheck.c -ldl
 * Use:   LD_PRELOAD=build/leakcheck.so <program>   -> report on stderr at exit:
 *        live allocations (count, bytes) and a backtrace per distinct call site. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LIVE 262144
#define DEPTH 12

typedef struct { void *p; size_t n; void *bt[DEPTH]; int d; } rec_t;
static rec_t live[MAX_LIVE];
static size_t n_live, peak_live, total_allocs, total_bytes, peak_bytes, cur_bytes, lost_track;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void (*real_free)(void *);
static __thread int inside;
static char boot[65536]; static size_t boot_used;   /* bootstrap arena before dlsym resolves */

static void init(void) {
    if (real_malloc) return;
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_free = dlsym(RTLD_NEXT, "free");
}
static int in_boot(void *p) { return (char *)p >= boot && (char *)p < boot + sizeof boot; }

static void track(void *p, size_t n) {
    pthread_mutex_lock(&mu);
    total_allocs++; total_bytes += n; cur_bytes += n; if (cur_bytes > peak_bytes) peak_bytes = cur_bytes;
    if (n_live < MAX_LIVE) {
        rec_t *r = &live[n_live++]; r->p = p; r->n = n;
        inside++; r->d = backtrace(r->bt, DEPTH); inside--;
        if (n_live > peak_live) peak_live = n_live;
    } else lost_track++;
    pthread_mutex_unlock(&mu);
}
static void untrack(void *p) {
    pthread_mutex_lock(&mu);
    for (size_t i = n_live; i-- > 0;) if (live[i].p == p) { cur_bytes -= live[i].n; live[i] = live[--n_live]; break; }
    pthread_mutex_unlock(&mu);
}

void *malloc(size_t n) {
    init();
    if (!real_malloc) { void *p = boot + boot_used; boot_used += (n + 15) & ~(size_t)15; return p; }
    void *p = real_malloc(n);
    if (p && !inside) track(p, n);
    return p;
}
void *calloc(size_t a, size_t b) {
    init();
    if (!real_calloc) { void *p = boot + boot_used; boot_used += (a * b + 15) & ~(size_t)15; memset(p, 0, a * b); return p; }
    void *p = real_calloc(a, b);
    if (p && !inside) track(p, a * b);
    return p;
}
void *realloc(void *q, size_t n) {
    init();
    if (in_boot(q)) { void *p = malloc(n); if (q) memcpy(p, q, n); return p; }
    if (q && !inside) untrack(q);
    void *p = real_realloc(q, n);
    if (p && !inside) track(p, n);
    return p;
}
void free(void *p) {
    init();
    if (!p || in_boot(p)) return;
    if (!inside) untrack(p);
    real_free(p);
}

static int same_site(const rec_t *a, const rec_t *b) {
    if (a->d != b->d) return 0;
    return memcmp(a->bt, b->bt, (size_t)a->d * sizeof(void *)) == 0;
}

__attribute__((destructor)) static void report(void) {
    inside++;
    fprintf(stderr, "\n[leakcheck] allocations: %zu (%zu bytes); peak live %zu (%zu bytes); live at exit %zu (%zu bytes)%s\n",
            total_allocs, total_bytes, peak_live, peak_bytes, n_live, cur_bytes, lost_track ? " [table overflow: some untracked]" : "");
    int shown = 0;
    for (size_t i = 0; i < n_live && shown < 40; i++) {
        if (live[i].p == NULL) continue;
        size_t count = 0, bytes = 0;
        for (size_t j = i; j < n_live; j++) if (live[j].p && same_site(&live[i], &live[j])) { count++; bytes += live[j].n; if (j != i) live[j].p = NULL; }
        fprintf(stderr, "[leakcheck] site: %zu live allocation(s), %zu bytes\n", count, bytes);
        backtrace_symbols_fd(live[i].bt, live[i].d, 2);
        shown++;
    }
    inside--;
}

/* tools/web/shmshim.c - LD_PRELOAD shim for WebKit (Playwright) in the OpenShell sandbox, where memfd_create fails with EPERM and /dev/shm is not writable.
 * WebKit's shared memory (UI process <-> web process) then cannot be created and every page hangs. The shim backs both calls with an unlinked file in
 * $TMPDIR (default /tmp), which mmap shares just as well. Seals (fcntl F_ADD_SEALS) are not available on such a file: the shim answers the sealing
 * commands with success. Used only for the WebKit engine of tools/web/smoke.py (set by tools/web/pw-env.sh); nothing of the game's own runs under it.
 *   cc -O1 -shared -fPIC -o /sandbox/pwlibs/lib/libshmshim.so tools/web/shmshim.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#endif

static int anon_file(void) {
    const char *d = getenv("TMPDIR");
    char path[512];
    snprintf(path, sizeof path, "%s/.shmshim-XXXXXX", d && d[0] ? d : "/tmp");
    int fd = mkostemp(path, O_CLOEXEC);
    if (fd >= 0) unlink(path);
    return fd;
}

int memfd_create(const char *name, unsigned flags) { (void)name; (void)flags; return anon_file(); }
int shm_open(const char *name, int oflag, mode_t mode) { (void)name; (void)oflag; (void)mode; return anon_file(); }
int shm_unlink(const char *name) { (void)name; return 0; }

int fcntl(int fd, int cmd, ...) {
    static int (*real)(int, int, ...) = NULL;
    if (!real) real = (int (*)(int, int, ...))dlsym(RTLD_NEXT, "fcntl");
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (cmd == F_ADD_SEALS) return 0;
    if (cmd == F_GET_SEALS) return 0;
    return real(fd, cmd, arg);
}

int fcntl64(int fd, int cmd, ...) __attribute__((alias("fcntl")));

/* WebKit also calls memfd_create through syscall(2) */
long syscall(long n, ...) {
    static long (*real)(long, long, long, long, long, long, long) = NULL;
    if (!real) real = (long (*)(long, long, long, long, long, long, long))dlsym(RTLD_NEXT, "syscall");
    va_list ap;
    va_start(ap, n);
    long a1 = va_arg(ap, long), a2 = va_arg(ap, long), a3 = va_arg(ap, long), a4 = va_arg(ap, long), a5 = va_arg(ap, long), a6 = va_arg(ap, long);
    va_end(ap);
    if (n == 319) return anon_file();   /* SYS_memfd_create on x86-64 */
    return real(n, a1, a2, a3, a4, a5, a6);
}

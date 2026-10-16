# leakcheck

An `LD_PRELOAD` allocation tracker for leak profiling in the sandbox, where valgrind
cannot start (the system `ld.so` is stripped and no debuginfo package can be installed)
and LeakSanitizer cannot attach (ptrace is denied). It wraps `malloc`, `calloc`,
`realloc` and `free`, keeps every live allocation with a backtrace, and prints at exit
the totals and one backtrace per distinct call site that still holds memory.

    cc -O1 -g -shared -fPIC -o build/leakcheck.so tools/leakcheck/leakcheck.c -ldl
    LD_PRELOAD=$PWD/build/leakcheck.so SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        build/interceptor --flight --frames 1200 --dump /tmp/f.ppm 2>leak.txt >/dev/null
    LD_PRELOAD=$PWD/build/leakcheck.so build/tests/test_flight 2>&1 | grep leakcheck

Read the "live at exit" line first: a count that does not grow with `--frames` means
no per-frame leak. Sites inside `libSDL` reached from `main` are the library's one-time
state. Memory errors (not leaks) are better found with
`make BUILD=/tmp/asan CFLAGS="-std=c99 -Wall -Wextra -O1 -g -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined" test`
with `ASAN_OPTIONS=detect_leaks=0`.

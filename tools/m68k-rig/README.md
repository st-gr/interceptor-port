# m68k-rig

A small Musashi-based 68000 harness for differential tests: it calls isolated pure
functions of the original game code with chosen inputs, so the port's results can be
compared against the original's.

- Build and test: `make test` (needs gcc and make; Musashi's `m68kmake` generates
  `m68kops.c/h` on first build).
- `musashi/` is a git submodule (https://github.com/kstenerud/Musashi); run
  `git submodule update --init` after cloning. `m68kconf.h` is the rig's own copy of
  the Musashi config so upstream changes cannot alter behaviour.
- API in `rig.h`: `rig_init`, `rig_load_image`, `rig_poke`, `rig_peek`, `rig_call`
  (C ABI, args on the stack, result in D0), `rig_shutdown`.
- The game image is produced by the hunk relocator (Task 4) into the git-ignored
  `extracted/interceptor.bin`, loaded at base 0x10000. Never commit it. Without it
  the smoke test is skipped. The rig reads only the flat .bin, never the map JSON.

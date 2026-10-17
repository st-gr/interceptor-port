#!/bin/bash
# Parser test: every committed script must parse, and bad lines must exit 2. Needs build/vamiga-ref/ref and disks/kick12.rom.
set -u
REF=${REF:-build/vamiga-ref/ref}; ROM=${ROM:-disks/kick12.rom}
fail=0
if [ ! -x "$REF" ] || [ ! -f "$ROM" ] || [ ! -f extracted/interceptor.bin ]; then echo "skipped: $REF, $ROM or extracted/interceptor.bin missing"; exit 0; fi
for f in tools/vamiga-ref/scripts/*.txt; do
  $REF --rom $ROM --script "$f" --parse-only >/dev/null 2>&1 || { echo "FAIL parse $f"; fail=1; }
done
t=$(mktemp)
for bad in "holduntil UP F:4B2A2 lt" "holduntil UP F:4B2A2 eq 5" "holduntil NOKEY F:4B2A2 lt 5" "step x" "step 0" "peek zz 4" "peek 10" "peek F:10 0" "seglist nofile nofile" "colors a b" "freeze now" "log" "runto" "runto zz" "blittrace maybe" "blitstop zz" "audtrace maybe" "audtrace on dma" "wav" "wav x.wav" "wav x.wav go" "wav x.txt start" "runto 0 5 maybe" "blitregs a b" "pokefill F:10 4 abc" "copy F:10 F:20" "copy zz F:20 4" "copy F:10 F:20 0"; do
  echo "$bad" > $t
  $REF --rom $ROM --script $t --parse-only >/dev/null 2>&1; rc=$?
  [ $rc -eq 2 ] || { echo "FAIL expected exit 2 for '$bad', got $rc"; fail=1; }
done
rm -f $t
[ $fail -eq 0 ] && echo "parser tests ok"
exit $fail

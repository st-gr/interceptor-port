#!/usr/bin/env bash
# Review Focus 2: a planted 12-byte window stops the export; a clean tree exports as one commit, authored by
# the configured user; an existing --out is refused without --force; a failing bundle has its own exit code.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
script=$here/../export-public.sh
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
# The author of the public commit is the configured user, whatever the submodule is called.
printf '[user]\n\tname = Test Author\n\temail = author@example.invalid\n' > "$t/gitconfig"
export GIT_CONFIG_GLOBAL=$t/gitconfig
fail() { echo "FAIL: $*" >&2; exit 1; }

mkdir -p "$t/repo/src" "$t/repo/docs/images" "$t/repo/.github/workflows" "$t/assets" "$t/scratch"
# Invented "original": pseudo-random bytes (a fixed seed); 12-byte windows have many distinct values.
python3 - "$t" <<'PY'
import random, sys
t = sys.argv[1]; r = random.Random(7)
for p in ("assets/a.bin", "scratch/disk.adf", "scratch/prog.bin"):
    open("%s/%s" % (t, p), "wb").write(bytes(r.randrange(256) for _ in range(4096)))
PY
printf 'int main(void){return 0;}\n' > "$t/repo/src/main.c"
printf 'all:\n\t@true\n' > "$t/repo/Makefile"
# The files the export takes from the ref (not from the working tree of the exporting checkout).
mkdir -p "$t/repo/tools/release" "$t/repo/tools/cleanroom"
cp "$here/../README.public.md" "$here/../pages.yml" "$here/../exclude.txt" "$t/repo/tools/release/"
cp "$here/../../cleanroom/scan.py" "$t/repo/tools/cleanroom/"
cp "$here/../../../LICENSE" "$t/repo/LICENSE"
cp "$here/../../../docs/images/interceptor-hero.png" "$t/repo/docs/images/interceptor-hero.png"
cp "$here/../../../.github/workflows/ci.yml" "$t/repo/.github/workflows/ci.yml"
cp "$here/../../../.github/workflows/release.yml" "$t/repo/.github/workflows/release.yml"
cp "$t/repo/.github/workflows/ci.yml" "$t/seeding-ci.yml"
mkdir -p "$t/repo/openshell"; echo profile > "$t/repo/openshell/p.yaml"
# A local bare repository as the submodule.
git init -q -b main "$t/subw"
echo sub > "$t/subw/f.txt"
git -C "$t/subw" -c user.name=t -c user.email=t@t add -A
git -C "$t/subw" -c user.name=t -c user.email=t@t commit -q -m sub
subsha=$(git -C "$t/subw" rev-parse HEAD)
git clone -q --bare "$t/subw" "$t/sub.git"
modules() { printf '[submodule "tools/sub"]\n\tpath = tools/sub\n\turl = %s\n' "$1" > "$t/repo/.gitmodules"; }
modules "$t/sub.git"
g() { git -C "$t/repo" -c user.name=t -c user.email=t@t "$@"; }
g init -q -b main
g add -A
g update-index --add --cacheinfo "160000,$subsha,tools/sub"
g commit -q -m scratch
args=(--assets "$t/assets" --disk "$t/scratch/disk.adf" --bin "$t/scratch/prog.bin" --github-repo test-owner/test-port --repo "$t/repo")

# Clean tree.
"$script" --out "$t/out/pub" "${args[@]}" >/dev/null || fail "clean export failed"
p() { git -C "$t/out/pub" "$@"; }
[ "$(p rev-list --count HEAD)" = 1 ] || fail "not one commit"
for f in README.md LICENSE .github/workflows/pages.yml src/main.c Makefile; do
  p cat-file -e "HEAD:$f" || fail "missing $f"
done
p cat-file -e HEAD:docs/images/interceptor-hero.png || fail "missing README hero image"
p cat-file -e HEAD:.github/workflows/ci.yml || fail "missing .github/workflows/ci.yml"
p cat-file -e HEAD:.github/workflows/release.yml || fail "missing .github/workflows/release.yml"
! p show HEAD:.github/workflows/ci.yml | grep -Fq 'tools/release/tests/test_export.sh' || fail "public workflow runs private export test"
cmp -s "$t/repo/.github/workflows/ci.yml" "$t/seeding-ci.yml" || fail "seeding workflow was changed"
grep -Fq 'tools/release/tests/test_export.sh' "$t/repo/.github/workflows/ci.yml" || fail "seeding workflow lacks export test"
p show HEAD:README.md | grep -Fq 'https://test-owner.github.io/test-port/' || fail "README.md lacks the Pages URL"
! p show HEAD:README.md | grep -Fq 'https://<user>.github.io/<repo>/' || fail "README.md retains the generic Pages URL"
p show HEAD:README.md | grep -Fq 'https://github.com/test-owner/test-port/releases/latest' || fail "README.md lacks the release URL"
! p show HEAD:README.md | grep -Fq '<owner>/<repo>' || fail "README.md retains the generic repository slug"
[ "$(p log -1 --format=%an)" = "Test Author" ] || fail "author is $(p log -1 --format=%an)"
[ "$(p log -1 --format=%ae)" = "author@example.invalid" ] || fail "author email"
p log -1 --format=%B | grep -q '^Co-Authored-By: Claude Fable 5.1' || fail "trailer"
{ [ -f "$t/out/pub.bundle" ] && git bundle verify "$t/out/pub.bundle" >/dev/null 2>&1; } || fail "bundle"
[ "$(ls "$t/out" | wc -l | tr -d ' ')" = 2 ] || fail "staging left"

# An existing --out is refused (exit 2, untouched) unless --force.
echo keep > "$t/out/pub/KEEP"
rc=0; "$script" --out "$t/out/pub" "${args[@]}" > "$t/logx" 2>&1 || rc=$?
[ "$rc" = 2 ] || fail "existing out: exit $rc, expected 2"
grep -q -- '--force' "$t/logx" || fail "existing out: no hint"
[ -f "$t/out/pub/KEEP" ] || fail "existing out was touched"
"$script" --force --out "$t/out/pub" "${args[@]}" >/dev/null || fail "--force export failed"
[ ! -e "$t/out/pub/KEEP" ] || fail "--force did not replace"

# A failing git bundle create: exit 4, the tree written, no bundle.
mkdir -p "$t/shim"
printf '#!/bin/sh\nfor a in "$@"; do [ "$a" = bundle ] && exit 1; done\nexec %s "$@"\n' "$(command -v git)" > "$t/shim/git"
chmod +x "$t/shim/git"
rc=0; PATH="$t/shim:$PATH" "$script" --out "$t/out/nb" "${args[@]}" > "$t/logb" 2>&1 || rc=$?
[ "$rc" = 4 ] || fail "bundle failure: exit $rc, expected 4"
[ -d "$t/out/nb/.git" ] && [ ! -e "$t/out/nb.bundle" ] || fail "bundle failure: wrong files"
rm -rf "$t/out/nb"

# The excluded path is gone and listed; the submodule is back at its commit.
! p cat-file -e HEAD:openshell/p.yaml 2>/dev/null || fail "openshell/ not excluded"
"$script" --out "$t/out/pub2" "${args[@]}" > "$t/log2" 2>&1; grep -q 'excluded openshell' "$t/log2" || fail "exclusion not listed"
[ "$(p ls-tree HEAD tools/sub | awk '{print $1" "$3}')" = "160000 $subsha" ] || fail "submodule gitlink"
[ -f "$t/out/pub/tools/sub/f.txt" ] || fail "submodule not checked out"
[ "$(git -C "$t/out/pub/tools/sub" rev-parse HEAD)" = "$subsha" ] || fail "submodule commit"
rm -rf "$t/out/pub2" "$t/out/pub2.bundle"

# A submodule that cannot be fetched: exit 3, the tree still exported, the README documents the step.
modules "$t/missing.git"
g add .gitmodules
g commit -q -m badurl
rc=0; "$script" --out "$t/out/bad" "${args[@]}" > "$t/log" 2>&1 || rc=$?
[ "$rc" = 3 ] || fail "bad submodule url: exit $rc, expected 3"
grep -q 'submodule add' "$t/out/bad/README.md" || fail "README lacks the manual step"
[ "$(git -C "$t/out/bad" rev-list --count HEAD)" = 1 ] || fail "bad: not one commit"
rm -rf "$t/out/bad" "$t/out/bad.bundle"
modules "$t/sub.git"
g add .gitmodules
g commit -q -m goodurl

# Planted window: 12 bytes of the asset.
rm -rf "$t/out"
{ printf 'planted:'; dd if="$t/assets/a.bin" bs=1 skip=1000 count=12 2>/dev/null; } > "$t/repo/src/leak.dat"
g add src/leak.dat
g commit -q -m leak
rc=0; "$script" --out "$t/out/pub" "${args[@]}" > "$t/log" 2>&1 || rc=$?
[ "$rc" = 1 ] || fail "planted window: exit $rc, expected 1"
grep -q 'src/leak.dat' "$t/log" || fail "file not named"
[ ! -e "$t/out/pub" ] && [ ! -e "$t/out/pub.bundle" ] || fail "output left behind"
[ -z "$(ls "$t/out" 2>/dev/null)" ] || fail "staging left behind"
echo "ok"

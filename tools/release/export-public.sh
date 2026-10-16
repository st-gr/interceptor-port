#!/usr/bin/env bash
# Builds the public repository: a one-commit export of a git ref's tree, plus the public README, the
# licence and the Pages workflow, after the clean-room scan has passed. Never pushes anything.
#
#   export-public.sh --out DIR --assets DIR --disk FILE --bin FILE --github-repo OWNER/REPO [--ref main] [--repo DIR] [--force]
#
# Writes DIR (a git repository with one commit) and DIR.bundle. An existing DIR or DIR.bundle is refused
# unless --force. Everything that goes into the public tree (the README, the licence, the Pages workflow, the
# exclude list, the scan tool and its allow list) comes from the ref, never from the working tree.
# Exit status: 0 done; 1 the scan found a match (its output is printed, DIR is not written); 2 usage error or
# another failure (an existing --out without --force, a file missing at the ref); 3 a submodule could not be
# fetched (the tree is exported, the README documents the manual step); 4 the bundle could not be created
# (DIR is written, DIR.bundle is not).
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
own_repo=$(cd "$here/../.." && pwd)
repo=$own_repo ref=main out= assets= disk= bin= github_repo= force=0
while [ $# -gt 0 ]; do
  case $1 in
    --out) out=$2; shift 2 ;;
    --assets) assets=$2; shift 2 ;;
    --disk) disk=$2; shift 2 ;;
    --bin) bin=$2; shift 2 ;;
    --github-repo) github_repo=$2; shift 2 ;;
    --ref) ref=$2; shift 2 ;;
    --repo) repo=$2; shift 2 ;;
    --force) force=1; shift ;;
    *) echo "usage: $0 --out DIR --assets DIR --disk FILE --bin FILE --github-repo OWNER/REPO [--ref main] [--repo DIR] [--force]" >&2; exit 2 ;;
  esac
done
[ -n "$out" ] && [ -n "$assets" ] && [ -n "$disk" ] && [ -n "$bin" ] && [ -n "$github_repo" ] || { echo "export-public: --out, --assets, --disk, --bin and --github-repo are required" >&2; exit 2; }
case $github_repo in
  *[!A-Za-z0-9._/-]*|/*|*/|*/*/*|*//*|*..*) echo "export-public: --github-repo must be OWNER/REPO" >&2; exit 2 ;;
  */*) ;;
  *) echo "export-public: --github-repo must be OWNER/REPO" >&2; exit 2 ;;
esac
github_owner=${github_repo%%/*}
github_name=${github_repo#*/}
pages_url="https://$github_owner.github.io/$github_name/"
[ -d "$assets" ] && [ -f "$disk" ] && [ -f "$bin" ] || { echo "export-public: the assets, the disk image and the program image must exist (the scan needs them)" >&2; exit 2; }
repo=$(cd "$repo" && pwd)
git -C "$repo" rev-parse --verify --quiet "$ref^{commit}" >/dev/null || { echo "export-public: no such ref in $repo: $ref" >&2; exit 2; }

mkdir -p "$(dirname "$out")"
out=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")
case $out in "$repo"|"$repo"/*|/) echo "export-public: --out must be outside the repository" >&2; exit 2 ;; esac
if [ "$force" = 0 ] && { [ -e "$out" ] || [ -e "$out.bundle" ]; }; then
  echo "export-public: $out or $out.bundle exists; pass --force to replace it" >&2; exit 2
fi
assets=$(cd "$assets" && pwd)
disk=$(cd "$(dirname "$disk")" && pwd)/$(basename "$disk")
bin=$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")

stage="$out.partial.$$"
allow_tmp=
cleanup() { rm -rf "$stage"; [ -z "$allow_tmp" ] || rm -f "$allow_tmp"; }
trap cleanup EXIT
rm -rf "$stage"; mkdir -p "$stage"

# The tree of the ref only: ignored and untracked material cannot be in it.
git -C "$repo" archive "$ref" | tar -x -C "$stage"
for f in tools/release/README.public.md LICENSE .github/workflows/ci.yml .github/workflows/release.yml tools/release/pages.yml tools/release/exclude.txt tools/cleanroom/scan.py; do
  [ -f "$stage/$f" ] || { echo "export-public: $f is not in $ref of $repo" >&2; exit 2; }
done
mv "$stage/tools/release/README.public.md" "$stage/README.md"
sed "s|https://<user>\.github\.io/<repo>/|$pages_url|g" "$stage/README.md" > "$stage/README.md.public"
mv "$stage/README.md.public" "$stage/README.md"
mkdir -p "$stage/.github/workflows"
cp "$stage/tools/release/pages.yml" "$stage/.github/workflows/pages.yml"

# The release-export self-test belongs only to the private seeding repository. Require explicit sentinels
# so a workflow edit cannot silently leave that step in the generated public repository or remove another.
workflow="$stage/.github/workflows/ci.yml"
workflow_tmp="$workflow.public"
if ! awk '
  $0 == "      # BEGIN private release-export self-test" { if (inside || ++starts != 1) exit 1; inside=1; next }
  $0 == "      # END private release-export self-test" { if (!inside || ++ends != 1) exit 1; inside=0; next }
  !inside { print }
  END { if (inside || starts != 1 || ends != 1) exit 1 }
' "$workflow" > "$workflow_tmp"; then
  echo "export-public: .github/workflows/ci.yml must contain exactly one private release-export self-test block" >&2
  exit 2
fi
mv "$workflow_tmp" "$workflow"

# The explicit exclude list (path TAB reason): removed before the scan, listed in the output.
excluded=()
if [ -f "$stage/tools/release/exclude.txt" ]; then
  while IFS= read -r line || [ -n "$line" ]; do
    case $line in ''|'#'*) continue ;; esac
    path=${line%%$'\t'*}; path=${path%/}
    case $path in ''|/*|*..*) echo "export-public: bad exclude entry: $line" >&2; exit 2 ;; esac
    if [ -e "$stage/$path" ]; then rm -rf "${stage:?}/$path"; excluded+=("$line"); fi
  done < "$stage/tools/release/exclude.txt"
fi
for e in "${excluded[@]+"${excluded[@]}"}"; do echo "export-public: excluded ${e%%$'\t'*} (${e#*$'\t'})"; done

# The clean-room scan gates the export: the exported tree's own allow list, or an empty one.
allow="$stage/tools/cleanroom/allow.txt"
if [ ! -f "$allow" ]; then allow_tmp=$(mktemp); allow=$allow_tmp; fi
rc=0
python3 "$stage/tools/cleanroom/scan.py" --repo "$stage" --assets "$assets" --disk "$disk" --bin "$bin" --allow "$allow" || rc=$?
if [ $rc -ne 0 ]; then
  echo "export-public: the scan failed (status $rc); nothing was written" >&2
  [ $rc -eq 1 ] && exit 1 || exit 2
fi

author=$(git config user.name || true)
mail=$(git config user.email || true)
cd "$stage"
git init -q -b main

# The archive drops the gitlinks: re-create each submodule at its pinned commit. On a failure the tree is
# committed without it, the README says how to add it, and the exit status is 3.
subfail=0
while read -r mode _ sha path; do
  [ "$mode" = 160000 ] || continue
  sub_fail_note() {
    subfail=1
    git rm -q --cached -f "$path" >/dev/null 2>&1 || true
    rm -rf "${stage:?}/$path" "${stage:?}/.git/modules/$subname"
    [ ! -f .gitmodules.orig ] || cp .gitmodules.orig .gitmodules
    printf '\n## Submodule\n\n`%s` is a git submodule (commit `%s`) that the export could not fetch. Add it by hand:\n\n    git submodule add %s %s\n    git -C %s checkout %s\n\n' \
      "$path" "$sha" "$url" "$path" "$path" "$sha" >> "$stage/README.md"
    echo "export-public: could not fetch the submodule $path from $url; the README documents the manual step" >&2
  }
  subname=$(git config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk -v p="$path" '$2==p{sub(/^submodule\./,"",$1);sub(/\.path$/,"",$1);print $1}')
  url=$(git config -f .gitmodules --get "submodule.$subname.url" || true)
  [ ! -f .gitmodules ] || cp .gitmodules .gitmodules.orig
  rm -rf "${stage:?}/$path"   # the archive leaves an empty directory where the gitlink was
  if [ -n "$url" ] && git -c protocol.file.allow=always submodule add -q "$url" "$path" >&2 \
     && git -C "$path" checkout -q "$sha" >/dev/null 2>&1; then
    :
  else
    sub_fail_note
  fi
  rm -f .gitmodules.orig
done < <(git -C "$repo" ls-tree -r "$ref" | awk '$1==160000')

git add -A
git -c user.name="${author:-Interceptor port}" -c user.email="${mail:-noreply@anthropic.com}" commit -q -m "Interceptor port: initial public release

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
cd /
rm -rf "$out" "$out.bundle"
mv "$stage" "$out"
if ! git -C "$out" bundle create "$out.bundle" --all >/dev/null 2>&1; then
  rm -f "$out.bundle"
  echo "export-public: wrote $out but git bundle create failed; exit status 4" >&2
  exit 4
fi

cat <<MSG
export-public: wrote $out (one commit $(git -C "$out" rev-parse --short HEAD)) and $out.bundle
Next steps:
  1. Create an empty repository on GitHub (no README, no licence file).
  2. cd $out && git remote add origin git@github.com:$github_repo.git && git push --mirror origin
     (or, from another machine: git clone $out.bundle <dir>, then add the remote and push).
  3. Settings, Pages, Source: GitHub Actions. The page appears at $pages_url after the first run.
MSG
[ "$subfail" = 0 ] || { echo "export-public: WARNING: a submodule could not be fetched (see above); exit status 3" >&2; exit 3; }

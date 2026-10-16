#!/usr/bin/env python3
"""Clean-room window scan.

Slides a 12-byte window over every git-tracked file of a repository (every regular file when the
directory is no git repository, for example an exported tree) and lists each window that also occurs
in the extracted assets, the disk image or the relocated program image.  Windows with fewer than 6
distinct byte values (counting sequences, padding) are ignored.

    scan.py --repo DIR [--assets DIR] [--disk FILE] [--bin FILE] [--allow FILE]

Output, one line per run of overlapping windows:  FILE OFFSET LENGTH SOURCE:OFFSET
Exit status: 0 no unallowed match, 1 matches listed, 2 usage or allow-list error (a stale line is
reported as 2 only when nothing else matched).

The allow list (default REPO/tools/cleanroom/allow.txt, never scanned itself) has one line per allowed run:
    FILE OFFSETS [src=SOURCE-PREFIX] [maxlen=N] REASON...
OFFSETS: the run's first byte, a comma list, LO-HI ranges, or * (any: for generated fixtures whose offsets move;
use with src= and maxlen=).  Text runs: "phrase | TEXT | REASON" allows any run of printable text that is part of TEXT.
A line without a reason is an error, and so is a line that suppresses nothing (a stale entry).
The extractor's listings (manifest.json, index.txt) are not sources. Sources that do not exist are skipped with a note on stderr, so the scan runs without assets in a
clone, where it only proves the allow list is consistent; the public export requires them.

Memory: the index holds one entry per distinct 8-byte prefix of an eligible window of the sources
(an int to an int); candidates are verified against the source bytes.
"""
import argparse
import os
import re
import subprocess
import sys

WIN = 12
MIN_DISTINCT = 6
PFX = 8


# Files the extractor writes about the assets (names, sizes, hashes): listings, not the original's data.
GENERATED = ("manifest.json", "index.txt")


def source_files(args):
    out = []
    if args.assets and os.path.isdir(args.assets):
        for root, _, names in sorted(os.walk(args.assets)):
            for n in sorted(names):
                if n in GENERATED:
                    continue
                p = os.path.join(root, n)
                out.append((os.path.relpath(p, os.path.dirname(os.path.abspath(args.assets))), p))
    elif args.assets:
        print("scan: note: no assets directory %s" % args.assets, file=sys.stderr)
    for p in (args.disk, args.bin):
        if p and os.path.isfile(p):
            out.append((os.path.basename(p), p))
        elif p:
            print("scan: note: no file %s" % p, file=sys.stderr)
    return out


def eligible(data):
    """Offsets of the windows of data with at least MIN_DISTINCT distinct bytes."""
    n = len(data)
    for i in range(n - WIN + 1):
        w = data[i:i + WIN]
        if len(set(w)) >= MIN_DISTINCT:
            yield i, w


def build_index(sources):
    blobs = []
    index = {}
    for label, path in sources:
        with open(path, "rb") as f:
            data = f.read()
        sid = len(blobs)
        blobs.append((label, data))
        for i, w in eligible(data):
            k = int.from_bytes(w[:PFX], "big")
            v = (sid << 40) | i
            cur = index.get(k)
            if cur is None:
                index[k] = v
            elif isinstance(cur, int):
                if cur != v:
                    index[k] = [cur, v]
            else:
                cur.append(v)
    return blobs, index


def lookup(blobs, index, w):
    cur = index.get(int.from_bytes(w[:PFX], "big"))
    if cur is None:
        return None
    for v in ([cur] if isinstance(cur, int) else cur):
        sid, off = v >> 40, v & ((1 << 40) - 1)
        if blobs[sid][1][off:off + WIN] == w:
            return blobs[sid][0], off
    return None


def repo_files(repo):
    if os.path.exists(os.path.join(repo, ".git")):
        r = subprocess.run(["git", "-C", repo, "ls-files", "-z"], capture_output=True)
        if r.returncode == 0:
            return [n.decode() for n in r.stdout.split(b"\0") if n]
    names = []
    for root, dirs, files in os.walk(repo):
        dirs[:] = [d for d in dirs if d != ".git"]
        for n in files:
            names.append(os.path.relpath(os.path.join(root, n), repo))
    return sorted(names)


def scan_file(path, blobs, index):
    """Runs (offset, length, source, source offset, bytes) of the matching windows of one file."""
    with open(path, "rb") as f:
        data = f.read()
    runs = []
    last_end = -1
    for i, w in eligible(data):
        hit = lookup(blobs, index, w)
        if not hit:
            continue
        if runs and i == last_end - WIN + 1:  # adjacent window: extend the run
            runs[-1][1] += 1
        else:
            runs.append([i, WIN, hit[0], hit[1]])
        last_end = i + WIN
    return [(o, n, src, so, data[o:o + n]) for o, n, src, so in runs]


def parse_offsets(text):
    """'*' (any), or a comma list of OFFSET and LO-HI."""
    if text == "*":
        return None
    out = []
    for part in text.split(","):
        if "-" in part:
            lo, hi = (int(x, 0) for x in part.split("-", 1))
        else:
            lo = hi = int(part, 0)
        out.append((lo, hi))
    return out


class Allow:
    """One allow line.  kind 'run': FILE OFFSETS [src=SOURCE] [maxlen=N] REASON.  kind 'phrase':
    phrase | TEXT [|| TEXT...] | REASON  (a run of printable text is allowed when its text, trimmed of blanks, newlines and NULs, is part of TEXT)."""

    def __init__(self, kind, reason, file=None, offsets=None, src=None, maxlen=None, text=None):
        self.kind, self.reason, self.file, self.offsets = kind, reason, file, offsets
        self.src, self.maxlen, self.text = src, maxlen, text
        self.used = False

    def matches(self, name, off, length, src, data):
        if self.kind == "phrase":
            core = data.strip(b" \t\r\n\0")
            if not core or not all(32 <= c < 127 for c in core):
                return False
            text = core.decode("latin-1")
            return any(text in t for t in self.text.split("||"))
        if self.file != name:
            return False
        if self.offsets is not None and not any(lo <= off <= hi for lo, hi in self.offsets):
            return False
        if self.src is not None and not src.startswith(self.src):
            return False
        return self.maxlen is None or length <= self.maxlen

    def describe(self):
        if self.kind == "phrase":
            return "phrase | %s | %s" % (self.text, self.reason)
        return "%s %s %s" % (self.file, self.offsets if self.offsets is not None else "*", self.reason)


def load_allow(path):
    """List of Allow; raises ValueError with the message on a bad line."""
    entries = []
    with open(path) as f:
        for ln, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            where = "%s:%d" % (path, ln)
            if s.startswith("phrase |"):
                parts = [x.strip() for x in re.split(r"\s\|\s", s)]
                if len(parts) < 3 or not parts[1] or not parts[2]:
                    raise ValueError("%s: phrase line without a text or a reason: %s" % (where, s))
                entries.append(Allow("phrase", " | ".join(parts[2:]), text=parts[1]))
                continue
            parts = s.split(None, 2)
            if len(parts) < 3:
                raise ValueError("%s: allow line without a reason: %s" % (where, s))
            try:
                offsets = parse_offsets(parts[1])
            except ValueError:
                raise ValueError("%s: bad offset %r" % (where, parts[1]))
            rest, src, maxlen = parts[2], None, None
            while True:
                tok = rest.split(None, 1)
                if tok and tok[0].startswith("src="):
                    src = tok[0][4:]
                elif tok and tok[0].startswith("maxlen="):
                    maxlen = int(tok[0][7:])
                else:
                    break
                rest = tok[1] if len(tok) > 1 else ""
            if not rest.strip():
                raise ValueError("%s: allow line without a reason: %s" % (where, s))
            entries.append(Allow("run", rest.strip(), file=parts[0], offsets=offsets, src=src, maxlen=maxlen))
    return entries


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", required=True)
    ap.add_argument("--assets", default="assets")
    ap.add_argument("--disk", default="extracted/interceptor.adf")
    ap.add_argument("--bin", default="extracted/interceptor.bin")
    ap.add_argument("--allow", default=None)
    ap.add_argument("--show", action="store_true",
                    help="append the matched bytes in hex (local analysis; keep the output out of the repository)")
    args = ap.parse_args(argv)

    allow_path = args.allow or os.path.join(args.repo, "tools", "cleanroom", "allow.txt")
    allow = []
    if os.path.isfile(allow_path):
        try:
            allow = load_allow(allow_path)
        except ValueError as e:
            print("scan: error: %s" % e, file=sys.stderr)
            return 2
    elif args.allow:
        print("scan: error: no allow file %s" % args.allow, file=sys.stderr)
        return 2

    sources = source_files(args)
    if not sources:
        print("scan: error: no source to scan against (assets, disk, image all missing)", file=sys.stderr)
        return 2
    blobs, index = build_index(sources)

    found = 0
    for name in repo_files(args.repo):
        path = os.path.join(args.repo, name)
        if os.path.islink(path) or not os.path.isfile(path):
            continue
        if os.path.abspath(path) == os.path.abspath(allow_path):
            continue   # the allow list names the strings it allows
        for off, length, src, soff, data in scan_file(path, blobs, index):
            ok = False
            for e in allow:
                if e.matches(name, off, length, src, data):
                    e.used = ok = True
            if not ok:
                found += 1
                line = "%s %d %d %s:%d" % (name, off, length, src, soff)
                if args.show:
                    line += " " + data.hex()
                print(line)
    stale = [e for e in allow if not e.used]
    for e in stale:
        print("scan: error: stale allow line (suppresses no match): %s" % e.describe(), file=sys.stderr)
    if found:
        return 1
    return 2 if stale else 0


if __name__ == "__main__":
    sys.exit(main())

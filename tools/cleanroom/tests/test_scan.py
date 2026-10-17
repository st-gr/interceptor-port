"""Tests of tools/cleanroom/scan.py on scratch trees: every asset byte string is invented here."""
import os
import subprocess
import sys

import pytest

SCAN = os.path.join(os.path.dirname(__file__), "..", "scan.py")

ASSET = bytes([7, 200, 31, 94, 3, 250, 18, 77, 140, 9, 61, 222, 5, 111, 36, 180])
WINDOW = ASSET[2:14]  # 12 bytes, 12 distinct values


def run(tree, allow=None):
    cmd = [sys.executable, SCAN, "--repo", str(tree / "repo"), "--assets", str(tree / "assets"),
           "--disk", str(tree / "disk.adf"), "--bin", str(tree / "image.bin")]
    if allow is not None:
        cmd += ["--allow", str(allow)]
    return subprocess.run(cmd, capture_output=True, text=True)


@pytest.fixture
def tree(tmp_path):
    (tmp_path / "assets" / "tables").mkdir(parents=True)
    (tmp_path / "assets" / "tables" / "t.bin").write_bytes(b"\0" * 5 + ASSET + b"\1" * 5)
    (tmp_path / "disk.adf").write_bytes(bytes(range(0, 256, 3)) * 2)
    (tmp_path / "image.bin").write_bytes(bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]))
    (tmp_path / "repo" / "src").mkdir(parents=True)
    (tmp_path / "repo" / "src" / "clean.c").write_bytes(b"int x;\n" * 20)
    return tmp_path


def plant(tree, data=WINDOW, at=30):
    path = tree / "repo" / "src" / "planted.bin"
    path.write_bytes(b"A" * at + data + b"B" * 10)
    return path


def test_clean_tree_exits_zero(tree):
    r = run(tree)
    assert r.returncode == 0, r.stdout + r.stderr


def test_planted_window_found(tree):
    plant(tree)
    r = run(tree)
    assert r.returncode == 1
    assert "src/planted.bin" in r.stdout
    assert " 30 " in r.stdout and " 12 " in r.stdout
    assert "assets/tables/t.bin" in r.stdout  # the matched source
    assert "clean.c" not in r.stdout


def test_longer_run_merged(tree):
    plant(tree, ASSET, at=4)
    r = run(tree)
    assert r.returncode == 1
    lines = [l for l in r.stdout.splitlines() if "planted.bin" in l]
    assert len(lines) == 1 and " 4 " in lines[0] and " 16 " in lines[0]


def test_disk_and_bin_sources(tree):
    (tree / "repo" / "a.dat").write_bytes(b"xx" + bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]))
    r = run(tree)
    assert r.returncode == 1 and "image.bin" in r.stdout


def test_few_distinct_bytes_ignored(tree):
    # a counting-like window with five distinct values is not a match, even though it is in the image
    (tree / "image.bin").write_bytes(bytes([1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1, 2, 3, 4, 5]))
    (tree / "repo" / "c.dat").write_bytes(bytes([1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1, 2]))
    assert run(tree).returncode == 0


def test_short_window_ignored(tree):
    plant(tree, WINDOW[:11])
    assert run(tree).returncode == 0


def test_allow_line_suppresses(tree):
    plant(tree)
    allow = tree / "allow.txt"
    allow.write_text("# comment\nsrc/planted.bin 30 invented counting scaffold\n")
    r = run(tree, allow)
    assert r.returncode == 0, r.stdout + r.stderr


def test_allow_other_offset_does_not_suppress(tree):
    plant(tree)
    allow = tree / "allow.txt"
    allow.write_text("src/planted.bin 31 wrong offset\n")
    assert run(tree, allow).returncode == 1


def test_allow_without_reason_is_error(tree):
    plant(tree)
    allow = tree / "allow.txt"
    allow.write_text("src/planted.bin 30\n")
    r = run(tree, allow)
    assert r.returncode == 2
    assert "reason" in r.stderr


def test_stale_allow_line_is_error(tree):
    allow = tree / "allow.txt"
    allow.write_text("src/gone.bin 0 nothing matches here any more\n")
    r = run(tree, allow)
    assert r.returncode == 2 and "stale" in r.stderr


def test_git_tracked_only(tree):
    repo = tree / "repo"
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    subprocess.run(["git", "-C", str(repo), "add", "src/clean.c"], check=True)
    plant(tree)  # untracked: not part of the repository
    assert run(tree).returncode == 0
    subprocess.run(["git", "-C", str(repo), "add", "src/planted.bin"], check=True)
    assert run(tree).returncode == 1


def test_phrase_allows_text_run(tree):
    (tree / "assets" / "t.txt").write_bytes(b"xx an invented sentence yy")
    (tree / "repo" / "doc.md").write_bytes(b"say: an invented sentence!")
    assert run(tree).returncode == 1
    allow = tree / "allow.txt"
    allow.write_text("phrase | an invented sentence | plain wording, invented for the test\n")
    assert run(tree, allow).returncode == 0
    allow.write_text("phrase | an invented | too short to cover the run\n")
    assert run(tree, allow).returncode == 1


def test_wildcard_offset_with_source_and_length_limits(tree):
    plant(tree)
    allow = tree / "allow.txt"
    allow.write_text("src/planted.bin * src=assets/tables/ maxlen=12 header scaffolding\n")
    assert run(tree, allow).returncode == 0
    allow.write_text("src/planted.bin * src=extracted/ header scaffolding\n")
    assert run(tree, allow).returncode == 1   # the other source
    allow.write_text("src/planted.bin * maxlen=11 header scaffolding\n")
    assert run(tree, allow).returncode == 1   # the run is longer


def test_allow_list_is_not_scanned(tree):
    (tree / "assets" / "t.txt").write_bytes(b"xx an invented sentence yy")
    (tree / "repo" / "tools" / "cleanroom").mkdir(parents=True)
    (tree / "repo" / "tools" / "cleanroom" / "allow.txt").write_text(
        "phrase | an invented sentence | the allow list names what it allows\n")
    # scanned, the allow file's own text would be a match the phrase allows (exit 0); skipped, the phrase is stale
    r = run(tree)
    assert r.returncode == 2 and "stale" in r.stderr


# The limits documented in the README. Each is a known negative: the scan does not see it, and these tests pin that, so a
# change of the scan's rules shows up as a deliberate edit of the README and of the tests.
def test_documented_limit_table_of_small_numbers_is_not_seen(tree):
    import struct
    table = struct.pack(">3L", 0x101, 0x203, 0x102)            # three longs of small values: at most 4 distinct bytes
    (tree / "image.bin").write_bytes(b"\xff" * 4 + table + b"\xfe" * 4)
    (tree / "repo" / "tbl.dat").write_bytes(b"zz" + table)
    assert len(set(table)) < 6
    assert run(tree).returncode == 0


def test_documented_limit_numbers_printed_as_text_are_not_seen(tree):
    values = [ASSET[i] for i in range(2, 14)]
    (tree / "repo" / "note.md").write_text(", ".join(str(v) for v in values) + "\n")   # the asset's bytes in decimal
    (tree / "repo" / "note2.md").write_text(" ".join("%02X" % v for v in values) + "\n")  # and in hex
    assert run(tree).returncode == 0


def test_an_offset_scoped_allow_line_does_not_hide_another_window_of_the_file(tree):
    path = tree / "repo" / "src" / "planted.bin"
    path.write_bytes(b"A" * 30 + WINDOW + b"B" * 10 + ASSET[4:16] + b"C" * 4)   # two windows of the asset, at 30 and 52
    allow = tree / "allow.txt"
    allow.write_text("src/planted.bin 30 src=assets/tables/ maxlen=12 the first window only\n")
    r = run(tree, allow)
    assert r.returncode == 1 and " 52 " in r.stdout

"""Writes the synthetic disk of the decoder tests (tests/synth.py, invented bytes) for the JavaScript port's tests.

    python tools/extract/tests/write_synth.py tools/extract-js/test/fixtures/synth.adf [expected.tar] [--rebuild]

The ADF is built exactly as tests/test_cli.py builds its disk (xdftool from amitools), unless the file exists already and
--rebuild is not given: the committed disk is kept (a rebuilt one differs in its time stamps), and only the expected output is
regenerated from it. With a second argument the Python extractor is run on the disk (with --allow-flat-spans: the synthetic disk has no segment table) and the whole output
directory is written as a deterministic tar (sorted names, zero times, mode 0644), the expected output of
tools/extract-js/test/. Nothing here comes from the game."""
import io
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

EXTRACT_DIR = Path(__file__).resolve().parents[1]      # tools/extract
sys.path.insert(0, str(EXTRACT_DIR))                    # for "tests" and "extract"
from tests.test_cli import build_adf   # noqa: E402


def write_tar(src: Path, dest: Path) -> None:
    with tarfile.open(dest, "w", format=tarfile.USTAR_FORMAT) as tf:
        for p in sorted(q for q in src.rglob("*") if q.is_file()):
            data = p.read_bytes()
            ti = tarfile.TarInfo(p.relative_to(src).as_posix())
            ti.size, ti.mtime, ti.mode, ti.uid, ti.gid, ti.uname, ti.gname = len(data), 0, 0o644, 0, 0, "", ""
            tf.addfile(ti, io.BytesIO(data))


def main(argv) -> int:
    rebuild = "--rebuild" in argv
    argv = [a for a in argv if a != "--rebuild"]
    if len(argv) not in (1, 2):
        print(__doc__, file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="synth-") as t:
        t = Path(t)
        if rebuild or not Path(argv[0]).is_file():
            adf = build_adf(t)
            Path(argv[0]).write_bytes(adf.read_bytes())
        else:
            adf = Path(argv[0]).resolve()
        if len(argv) == 2:
            out = t / "assets"
            r = subprocess.run([sys.executable, "-m", "extract", str(adf), str(out), "--allow-flat-spans"],
                               capture_output=True, text=True, cwd=EXTRACT_DIR)
            if r.returncode:
                print(r.stderr, file=sys.stderr)
                return 1
            write_tar(out, Path(argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

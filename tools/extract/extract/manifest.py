import hashlib
import json
from pathlib import Path

EXTRACTOR_VERSION = 27


def sha256_of(path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_manifest(out_dir: Path, source_path, sample_rates: dict, spans_relocated: bool = True) -> None:
    """sample_rates: sample file name -> WAV rate in Hz (note 10 2.4)."""
    files = {}
    for p in sorted(out_dir.rglob("*")):
        if p.is_file() and p.name != "manifest.json":
            files[p.relative_to(out_dir).as_posix()] = sha256_of(p)
    manifest = {
        "extractor_version": EXTRACTOR_VERSION,
        "source_sha256": sha256_of(source_path),
        "sample_rate_hz": sample_rates,
        # False: the flight tables' junk spans carry the flat values of their relocated longs (no segment table); the core refuses such assets
        "spans_relocated": spans_relocated,
        "files": files,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

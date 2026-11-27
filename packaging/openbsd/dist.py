"""Build the checksummed ports distfile from tracked sources and pinned word data."""
import argparse
import base64
import hashlib
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "packaging/shared"))
sys.path.insert(0, str(ROOT / "packaging/release"))
from assets import project_version
from stage import stage_dictionary
from thesaurus import stage_thesaurus


def build(output, update):
    name = "crumbwriter-" + project_version()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / (name + ".tar.xz")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        word_data = tmp / "word-data"
        stage_dictionary(word_data, tmp)
        stage_thesaurus(word_data, tmp)
        tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
        entries = {s: ROOT / s for s in tracked if s and not s.startswith(".")
                   and s != "packaging/openbsd/port/distinfo"}
        for file in word_data.rglob("*"):
            if file.is_file():
                entries[file.relative_to(tmp).as_posix()] = file
        with tarfile.open(archive, "w:xz", format=tarfile.USTAR_FORMAT, preset=6) as tar:
            for path, file in sorted(entries.items()):
                info = tarfile.TarInfo(name + "/" + path)
                info.size = file.stat().st_size
                info.mode = 0o755 if file.stat().st_mode & 0o111 else 0o644
                with file.open("rb") as stream:
                    tar.addfile(info, stream)
    digest = base64.b64encode(hashlib.sha256(archive.read_bytes()).digest()).decode()
    manifest = f"SHA256 ({archive.name}) = {digest}\nSIZE ({archive.name}) = {archive.stat().st_size}\n"
    distinfo = ROOT / "packaging/openbsd/port/distinfo"
    if update:
        distinfo.write_text(manifest)
    elif distinfo.read_text() != manifest:
        sys.exit("Ports distinfo differs from this checkout. Regenerate it before releasing.")
    print(archive)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--update-distinfo", action="store_true")
    args = parser.parse_args()
    build(args.output, args.update_distinfo)

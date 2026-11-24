"""Stage the native jobs' tested downloads, with checksums for every file."""
import argparse
import hashlib
from pathlib import Path
import shutil

INSTALLERS = {
    "ubuntu-installer": "CrumbWriter-Ubuntu-24.04-amd64.deb",
    "freebsd-installer": "CrumbWriter-FreeBSD-15-amd64.pkg",
    "windows-installer": "CrumbWriter-Windows-x64-Setup.exe",
    "macos-installer": "CrumbWriter-macOS-Universal.dmg",
}
SOURCE_PACKAGES = (
    "windows-runtime-sources",
    "macos-runtime-sources-arm64",
    "macos-runtime-sources-x86_64",
)
# GitHub Releases has a 2 GiB limit per asset. Source archives may grow.
PART_SIZE = 1800 * 1024 * 1024


def stage(source, destination):
    if not source.is_file() or source.is_symlink() or not source.stat().st_size:
        raise ValueError(f"Missing or empty release asset: {source}")
    target = destination / source.name
    if target.exists():
        raise ValueError(f"Duplicate release asset: {source.name}")
    if source.stat().st_size <= PART_SIZE:
        shutil.move(source, target)
        return
    if source.name in INSTALLERS.values():
        raise ValueError(f"Installer exceeds the release asset limit: {source.name}")
    with source.open("rb") as stream:
        for index, offset in enumerate(range(0, source.stat().st_size, PART_SIZE), start=1):
            part = target.with_name(f"{target.name}.part{index:03d}")
            remaining = min(PART_SIZE, source.stat().st_size - offset)
            with part.open("xb") as output:
                while remaining:
                    block = stream.read(min(remaining, 1024 * 1024))
                    if not block:
                        raise OSError(f"Unexpected end of {source}")
                    output.write(block)
                    remaining -= len(block)
    source.unlink()


def collect(incoming, destination):
    destination.mkdir()
    for artifact, name in INSTALLERS.items():
        stage(incoming / artifact / name, destination)
    for artifact in SOURCE_PACKAGES:
        archives = sorted((incoming / artifact).glob("CrumbWriter-*"))
        if not archives:
            raise ValueError(f"No matching library sources in {artifact}")
        for path in archives:
            stage(path, destination)
    with (destination / "SHA256SUMS").open("x", encoding="ascii", newline="\n") as sums:
        for path in sorted(destination.iterdir()):
            if path.name != "SHA256SUMS":
                with path.open("rb") as stream:
                    sums.write(f"{hashlib.file_digest(stream, 'sha256').hexdigest()}  {path.name}\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("incoming", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    collect(args.incoming, args.destination)

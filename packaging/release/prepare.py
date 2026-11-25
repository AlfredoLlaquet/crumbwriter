"""Stage the native jobs' tested downloads, with checksums for every file."""
import argparse
import hashlib
from pathlib import Path
import shutil

from assets import installers, project_version

SOURCE_PACKAGES = {
    "windows-runtime-sources": "CrumbWriter-{version}-Windows-x64-Sources.tar",
    "macos-runtime-sources-arm64": "CrumbWriter-{version}-macOS-arm64-Sources.tar",
    "macos-runtime-sources-x86_64": "CrumbWriter-{version}-macOS-x86_64-Sources.tar",
}
# GitHub Releases has a 2 GiB limit per asset. Source archives may grow.
PART_SIZE = 1800 * 1024 * 1024


def stage(source, destination, allow_split=False):
    if not source.is_file() or source.is_symlink() or not source.stat().st_size:
        raise ValueError(f"Missing or empty release asset: {source}")
    target = destination / source.name
    if target.exists():
        raise ValueError(f"Duplicate release asset: {source.name}")
    if source.stat().st_size <= PART_SIZE:
        shutil.move(source, target)
        return
    if not allow_split:
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
    version = project_version()
    destination.mkdir()
    for target, name in installers(version).items():
        stage(incoming / f"{target}-installer" / name, destination)
    for artifact, template in SOURCE_PACKAGES.items():
        stage(incoming / artifact / template.format(version=version), destination, allow_split=True)
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

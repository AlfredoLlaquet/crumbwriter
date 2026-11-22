"""Collect matching library sources and their original copyright notices."""

import contextlib
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import time
import urllib.request
import zipfile


def sha256(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
        return digest.hexdigest()


def download(url, output, expected_hash=None):
    output.parent.mkdir(parents=True, exist_ok=True)
    part = output.with_suffix(output.suffix + ".part")
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "CrumbWriter-Packaging"})
            with urllib.request.urlopen(request, timeout=90) as response, part.open("wb") as stream:
                shutil.copyfileobj(response, stream)
                size = response.headers.get("Content-Length")
                if size is not None and stream.tell() != int(size):
                    raise OSError("incomplete transfer")
            if expected_hash and sha256(part) != expected_hash:
                raise ValueError(f"SHA-256 mismatch for {url}")
            part.replace(output)
            return
        except OSError:
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)
        finally:
            part.unlink(missing_ok=True)


def notice_filename(name):
    return (
        name == "qt_attribution.json"
        or bool(re.match(r"^(?:licen[cs]es?|copying|copyrights?|notices?)(?:$|[._ -])", name, re.I))
    )


def source_archive(path):
    return str(path).lower().endswith((".tar", ".tar.zst", ".tar.xz", ".tar.gz", ".tar.bz2", ".tar.lz", ".tgz", ".zip"))


class Notices:
    def __init__(self, folder):
        self.folder = folder
        self.index = {}
        folder.mkdir(parents=True, exist_ok=True)

    def add(self, name, data):
        # Keep source paths readable in the index and filenames short enough for Windows.
        digest = hashlib.sha256(data).hexdigest()[:16]
        base = re.sub(r"[^a-zA-Z0-9_.-]", "_", PurePosixPath(name).name)[:45]
        filename = digest + "-" + base
        dest = self.folder / filename
        if not dest.exists():
            dest.write_bytes(data)
        self.index[name] = filename

    def finish(self):
        if not self.index:
            raise RuntimeError(f"No copyright notices found for {self.folder.name}")
        (self.folder / "index.txt").write_text(
            "".join(f"{name} -> {filename}\n" for name, filename in sorted(self.index.items())),
            encoding="utf-8",
        )


def needed_notice(name):
    path = PurePosixPath(name)
    return notice_filename(path.name) or any(p.lower() == "licenses" for p in path.parts[:-1])


@contextlib.contextmanager
def tar_members(path):
    suffix = path.suffix
    if suffix in (".zst", ".lz"):
        program = "zstd" if suffix == ".zst" else "lzip"
        with path.open("rb") as raw, subprocess.Popen(
            [program, "-dc"], stdin=raw, stdout=subprocess.PIPE
        ) as process:
            with tarfile.open(fileobj=process.stdout, mode="r|") as archive:
                yield archive
            # Tar can finish before the compressor has emitted its final padding.
            while process.stdout.read(64 * 1024):
                pass
            process.stdout.close()
            if process.wait():
                raise RuntimeError(f"Unable to read {path}")
    else:
        with tarfile.open(path, mode="r|*") as archive:
            yield archive


def archive_notices(path, notices, nested=False):
    """MSYS source packages may wrap upstream release tarballs plus a PKGBUILD."""
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as archive:
            for item in archive.infolist():
                if not item.is_dir() and item.file_size <= 2_000_000 and needed_notice(item.filename):
                    notices.add(item.filename, archive.read(item))
        return
    with tar_members(path) as archive:
        for item in archive:
            if not item.isfile():
                continue
            if item.size <= 2_000_000 and needed_notice(item.name):
                notices.add(item.name, archive.extractfile(item).read())
            elif nested and source_archive(item.name):
                with tempfile.TemporaryDirectory() as tmp:
                    child = Path(tmp) / PurePosixPath(item.name).name
                    with child.open("wb") as out:
                        shutil.copyfileobj(archive.extractfile(item), out)
                    archive_notices(child, notices)


def source_index(folder):
    lines = []
    for file in sorted(folder.rglob("*")):
        if file.is_file() and file != folder / "SHA256SUMS.txt":
            lines.append(f"{sha256(file)}  {file.relative_to(folder).as_posix()}")
    (folder / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

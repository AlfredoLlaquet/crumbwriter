"""Archive the recipes and sources of bottles whose dylibs went into the app."""

import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
from urllib.parse import urlsplit

from source_files import Notices, archive_notices, download, notice_filename, sha256, source_archive


def brew(*args):
    return subprocess.check_output(["brew", *args], text=True).strip()


def formula_sources(recipe):
    # Read Homebrew's checksum-paired release archives, mirrors, resources and
    # URL patches. HEAD/livecheck URLs and platform bottle checksums are separate.
    pending = {}
    archives = []
    text = recipe.read_text(encoding="utf-8").split("\n__END__", 1)[0]
    for line in text.splitlines():
        match = re.match(r'^( *)(url|mirror|sha256) "([^"]+)"', line)
        if not match:
            continue
        indent, field, value = match.groups()
        if field == "url":
            pending[indent] = [value]
        elif field == "mirror" and indent in pending:
            pending[indent].append(value)
        elif field == "sha256":
            urls = pending.pop(indent, [])
            if not urls or not re.fullmatch(r"[0-9a-f]{64}", value):
                raise ValueError(f"Unrecognized checksummed source in {recipe}: {line}")
            if not urls[0].startswith("https://") or "#{" in urls[0]:
                raise ValueError(f"Unsupported source URL in {recipe}: {urls[0]}")
            archives.append((urls, value))
    if not archives:
        raise ValueError(f"Missing checksummed source in {recipe}")
    patches = re.findall(r'^\s*file "(Patches/[^"]+)"', text, re.MULTILINE)
    expected = len(re.findall(r'^\s*resource "[^"]+"[^\n]* do\s*$', text, re.MULTILINE))
    if len(archives) < 1 + expected:
        raise ValueError(f"Missing checksummed resource in {recipe}")
    return archives, patches


def stage_tap_patches(keg, recipe, patches, folder):
    if not patches:
        return
    receipt = json.loads((keg / "INSTALL_RECEIPT.json").read_text(encoding="utf-8"))
    revision = receipt.get("source", {}).get("tap_git_head")
    checksums = {}
    if not revision:
        info = json.loads(brew("info", "--json=v2", keg.parent.name))["formulae"][0]
        if info["ruby_source_checksum"]["sha256"] != sha256(recipe):
            raise ValueError(f"Cannot identify the Homebrew revision for {recipe}")
        revision = info["tap_git_head"]
        checksums = info.get("local_patch_checksums", {})
    if not re.fullmatch(r"[a-f0-9]{40}", revision):
        raise ValueError(f"Unexpected Homebrew source revision: {revision}")
    for name in patches:
        path = PurePosixPath(name)
        if ".." in path.parts or path.is_absolute():
            raise ValueError(f"Invalid formula patch path: {name}")
        url = f"https://raw.githubusercontent.com/Homebrew/homebrew-core/{revision}/{name}"
        download(url, folder / path, checksums.get(name))


def stage_formula(keg, destination, sources):
    name = keg.parent.name
    version = keg.name
    key = f"{name}-{version}"
    recipe = keg / ".brew" / (name + ".rb")
    folder = sources / key
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copy2(recipe, folder / recipe.name)
    notices = Notices(destination / "third-party" / key)
    for entry in keg.iterdir():
        if entry.is_file() and notice_filename(entry.name):
            notices.add(entry.name, entry.read_bytes())
    archives, patches = formula_sources(recipe)
    stage_tap_patches(keg, recipe, patches, folder)
    for urls, checksum in archives:
        target = folder / (checksum[:12] + "-" + PurePosixPath(urlsplit(urls[0]).path).name)
        mirrors = [url for url in urls if url.startswith("https://")]
        for index, url in enumerate(mirrors):
            try:
                download(url, target, checksum)
                break
            except OSError:
                if index == len(mirrors) - 1:
                    raise
        if source_archive(target):
            archive_notices(target, notices)
    notices.finish()
    return f"{name}  {version}\n  Exact Homebrew formula and source downloads: {key}/"


def stage_brew(destination, runtime_list, sources):
    cellar = Path(brew("--cellar")).resolve()
    kegs = set()
    for line in runtime_list.read_text(encoding="utf-8").splitlines():
        if line.strip():
            path = Path(line.strip()).resolve(strict=True)
            formula, version, *_ = path.relative_to(cellar).parts
            kegs.add(cellar / formula / version)
    if not kegs:
        raise RuntimeError("No Homebrew dylibs were recorded")
    return [stage_formula(keg, destination, sources) for keg in sorted(kegs)]


def stage_qt(destination, prefix, sources):
    version = subprocess.check_output([str(prefix / "bin/qmake"), "-query", "QT_VERSION"], text=True).strip()
    # These are the official Qt installers' LGPL libraries, all from qtbase.
    # Review the source hash whenever packaging/macos changes its pinned Qt.
    checksums = {"6.8.3": "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80"}
    if version not in checksums:
        raise RuntimeError(f"No matching source checksum for the staged Qt {version}")
    series = ".".join(version.split(".")[:2])
    name = f"qtbase-everywhere-src-{version}.tar.xz"
    url = f"https://download.qt.io/archive/qt/{series}/{version}/submodules/{name}"
    archive = sources / name
    download(url, archive, checksums[version])
    notices = Notices(destination / "third-party" / f"qtbase-{version}")
    archive_notices(archive, notices)
    notices.finish()
    return f"Qt base  {version}\n  {url}"

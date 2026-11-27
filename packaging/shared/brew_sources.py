"""Archive the recipes and sources of bottles whose dylibs went into the app."""

import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from urllib.parse import urlencode, urlsplit

from source_files import Notices, archive_notices, download, notice_filename, source_archive


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


def formula_body(path):
    # Bottling inserts or replaces this block after building; it contains the
    # published bottle hashes. Every other line of the recipe must match.
    body = re.sub(r"^  bottle do\n.*?^  end$", "", path.read_text(encoding="utf-8"), flags=re.MULTILINE | re.DOTALL)
    return [line for line in body.splitlines() if line.strip()]


def read_json(url, headers=None):
    with urlopen(Request(url, headers=headers or {}), timeout=45) as response:
        return json.load(response)


def matching_patch_revision(keg, recipe):
    name = keg.parent.name
    # The registry index records the exact git commit used to build a bottle.
    # Some poured INSTALL_RECEIPTs contain no tap_git_head.
    info = read_json(f"https://formulae.brew.sh/api/formula/{name}.json")
    version = info["versions"]["stable"]
    if info["revision"]:
        version += f"_{info['revision']}"
    tags = [keg.name]
    release = info.get("bottle", {}).get("stable", {}).get("rebuild", 0)
    if version == keg.name and release:
        tags.insert(0, f"{keg.name}-{release}")
    registry = "homebrew/core/" + name.replace("@", "/")
    token = read_json(f"https://ghcr.io/token?scope=repository:{registry}:pull&service=ghcr.io")["token"]
    headers = {"Authorization": "Bearer " + token, "Accept": "application/vnd.oci.image.index.v1+json"}
    installed_body = formula_body(recipe)
    with tempfile.TemporaryDirectory() as tmp:
        candidate = Path(tmp) / recipe.name
        for tag in tags:
            try:
                manifest = read_json(f"https://ghcr.io/v2/{registry}/manifests/{tag}", headers)
            except HTTPError as error:
                if error.code == 404:
                    continue
                raise
            metadata = manifest["annotations"]
            revision = metadata["org.opencontainers.image.revision"]
            source = urlsplit(metadata["org.opencontainers.image.source"]).path
            path = source.split(f"/blob/{revision}/", 1)[-1]
            download(f"https://raw.githubusercontent.com/Homebrew/homebrew-core/{revision}/{path}", candidate)
            if formula_body(candidate) == installed_body:
                return revision, {}
        # A locally compiled keg can match the official current recipe exactly.
        if version == keg.name:
            revision = info["tap_git_head"]
            download(f"https://raw.githubusercontent.com/Homebrew/homebrew-core/{revision}/{info['ruby_source_path']}",
                     candidate, info["ruby_source_checksum"]["sha256"])
            if formula_body(candidate) == installed_body:
                return revision, info.get("local_patch_checksums", {})
        # Older runner images can retain bottles whose version tag has since
        # been rebuilt. Only accept a historical recipe when its complete
        # build instructions still match the ones shipped in the installed keg.
        path = info["ruby_source_path"]
        query = urlencode({"path": path, "per_page": 100})
        history = read_json("https://api.github.com/repos/Homebrew/homebrew-core/commits?" + query)
        for commit in history:
            revision = commit["sha"]
            url = f"https://raw.githubusercontent.com/Homebrew/homebrew-core/{revision}/{path}"
            try:
                download(url, candidate)
            except HTTPError as error:
                if error.code == 404:
                    continue
                raise
            if formula_body(candidate) == installed_body:
                return revision, {}
    raise ValueError(f"Cannot identify the Homebrew source revision for {recipe}")


def stage_tap_patches(keg, recipe, patches, folder):
    if not patches:
        return
    receipt = json.loads((keg / "INSTALL_RECEIPT.json").read_text(encoding="utf-8"))
    revision = receipt.get("source", {}).get("tap_git_head")
    checksums = {}
    if not revision:
        revision, checksums = matching_patch_revision(keg, recipe)
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

"""Archive the recipes and sources of bottles whose dylibs went into the app."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from source_files import Notices, archive_notices, download, notice_filename, source_archive


def brew(*args):
    return subprocess.check_output(["brew", *args], text=True).strip()


def stage_formula(keg, destination, sources):
    name = keg.parent.name
    version = keg.name
    key = f"{name}-{version}"
    recipe = keg / ".brew" / (name + ".rb")
    folder = sources / key
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copy2(recipe, folder / recipe.name)
    notices = Notices(destination / "third-party" / key)
    # Homebrew installs upstream notices in each keg for most formulae.
    for entry in keg.iterdir():
        if entry.is_file() and notice_filename(entry.name):
            notices.add(entry.name, entry.read_bytes())
    with tempfile.TemporaryDirectory() as tmp:
        cache = Path(tmp)
        env = dict(os.environ, HOMEBREW_CACHE=tmp, HOMEBREW_NO_AUTO_UPDATE="1", HOMEBREW_NO_ANALYTICS="1")
        # Homebrew recognizes this .brew file as the recipe from the installed keg,
        # even after homebrew/core publishes a newer version.
        subprocess.run(["brew", "fetch", "--retry", "--build-from-source", "--formula", str(recipe)],
                       env=env, check=True)
        downloaded = [p for p in (cache / "downloads").glob("*") if p.is_file()]
        if not downloaded:
            raise RuntimeError(f"Homebrew fetched no sources for {key}")
        for path in sorted(downloaded):
            target = folder / path.name
            shutil.copy2(path, target)
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

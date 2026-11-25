"""Package the normal make install tree with native FreeBSD, Ubuntu or Debian tools."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "packaging/release"))
from assets import installers, project_version

WEBSITE = "https://github.com/AlfredoLlaquet/crumbwriter"
CONTACT = "AlfredoLlaquet@users.noreply.github.com"
SUMMARY = "Keyboard-driven editor for plain-text manuscripts"
DESCRIPTION = "Write numbered crumbs, novels and other prose in a word-wrapped Qt 6 editor."
FIXTURE_VERSION = "0.0.1"


def output(*args, cwd=None):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def installed_files(root):
    return sorted(path for path in root.rglob("*") if path.is_file())


def build_deb(stage, work, destination, version):
    # dpkg-shlibdeps reads the actual ELF symbol requirements against the host's
    # package database. It expects a debian/control in the working tree.
    debian = work / "debian"
    debian.mkdir(exist_ok=True)
    (debian / "control").write_text(
        f"Source: crumbwriter\nMaintainer: Crumb Writer <{CONTACT}>\n\n"
        f"Package: crumbwriter\nArchitecture: any\nDescription: {SUMMARY}\n",
        encoding="utf-8")
    deps = output("dpkg-shlibdeps", "-O", "-e" + str(stage / "usr/bin/crumbwriter"), cwd=work)
    if not deps.startswith("shlibs:Depends=") or "\n" in deps:
        raise ValueError(f"Unexpected dpkg-shlibdeps result: {deps}")
    runtime = deps.removeprefix("shlibs:Depends=")
    # Dependencies loaded as plugins or data never appear in the executable's ELF table.
    runtime += ", qt6-qpa-plugins, qt6-wayland, libxcb-cursor0, hunspell-en-us, mythes-en-us"
    files = [file for file in installed_files(stage) if "DEBIAN" not in file.relative_to(stage).parts]
    size = sum((file.stat().st_size + 1023) // 1024 for file in files)
    control = stage / "DEBIAN"
    control.mkdir(exist_ok=True)
    (control / "control").write_text(
        f"Package: crumbwriter\nVersion: {version}\nArchitecture: amd64\n"
        f"Maintainer: Crumb Writer <{CONTACT}>\nSection: editors\nPriority: optional\n"
        f"Installed-Size: {size}\nDepends: {runtime}\nHomepage: {WEBSITE}\n"
        f"Description: {SUMMARY}\n {DESCRIPTION}\n", encoding="utf-8")
    with (control / "md5sums").open("w", encoding="utf-8") as sums:
        for file in files:
            with file.open("rb") as source:
                sums.write(f"{hashlib.file_digest(source, 'md5').hexdigest()}  {file.relative_to(stage)}\n")
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", stage, destination], check=True)


def build_pkg(stage, work, destination, version):
    meta = work / "metadata"
    meta.mkdir(exist_ok=True)
    dependencies = {}
    for name in ("qt6-base", "qt6-wayland", "glib", "hunspell", "en-hunspell", "en-mythes"):
        origin, installed_version = output("pkg", "query", "%o %v", name).split()
        dependencies[name] = {"origin": origin, "version": installed_version}
    manifest = {
        "name": "crumbwriter", "origin": "editors/crumbwriter", "version": version,
        "comment": SUMMARY, "desc": DESCRIPTION, "maintainer": CONTACT, "www": WEBSITE,
        "prefix": "/usr/local", "categories": ["editors"], "deps": dependencies,
        "licenses": ["UNLICENSE", "MyThes", "CC0-1.0"], "licenselogic": "and",
        "abi": output("pkg", "config", "ABI"),
    }
    (meta / "+MANIFEST").write_text(json.dumps(manifest), encoding="utf-8")
    prefix = stage / "usr/local"
    plist = work / "plist"
    plist.write_text("@owner root\n@group wheel\n" +
                     "".join(f"{file.relative_to(prefix)}\n" for file in installed_files(prefix)),
                     encoding="utf-8")
    subprocess.run(["pkg", "create", "-m", meta, "-p", plist, "-r", stage, "-o", work], check=True)
    shutil.copy2(work / f"crumbwriter-{version}.pkg", destination)


def native_target():
    if platform.system() == "FreeBSD":
        abi = output("pkg", "config", "ABI").lower()
        for major in ("14", "15"):
            if abi == f"freebsd:{major}:amd64":
                return build_pkg, "/usr/local", f"freebsd{major}"
    if platform.system() == "Linux" and output("dpkg", "--print-architecture") == "amd64":
        distro = platform.freedesktop_os_release()
        target = distro["ID"] + distro["VERSION_ID"]
        if target in ("ubuntu24.04", "ubuntu26.04", "debian13"):
            return build_deb, "/usr", target
    raise SystemExit("Release builds require amd64 FreeBSD 14.x/15.x, Ubuntu 24.04/26.04 or Debian 13.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--upgrade-fixture", action="store_true", help="also build a CI-only, earlier package")
    args = parser.parse_args()
    build, prefix, target = native_target()
    version = project_version()
    filename = installers(version)[target]
    package_version = f"{version}-{target}" if build is build_deb else version
    args.destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        stage = work / "stage"
        subprocess.run(["make", "install", f"DESTDIR={stage}", f"PREFIX={prefix}"], cwd=REPOSITORY, check=True)
        payload = stage / prefix.lstrip("/")
        executable = payload / "bin/crumbwriter"
        if output(executable, "--version") != f"Crumb Writer {version}":
            raise ValueError("The installed executable version differs from src/qt/buildinfo.h.")
        subprocess.run(["strip", executable], check=True)
        docs = payload / "share/doc/crumbwriter"
        if build is build_deb:
            # Include the complete set of bundled source notices in Debian's standard location.
            notices = [docs / "LICENSE", docs / "COPYING-MyThes", REPOSITORY / "data/bsd-first-LICENSE"]
            (docs / "copyright").write_bytes(b"\n".join(path.read_bytes() for path in notices))
        build(stage, work, args.destination / filename, package_version)
        if args.upgrade_fixture:
            marker = payload / "share/crumbwriter/upgrade-fixture"
            marker.parent.mkdir(exist_ok=True)
            marker.write_text("Removed when the release replaces the CI fixture.\n")
            build(stage, work, args.destination / ("UpgradeFixture" + Path(filename).suffix), FIXTURE_VERSION)


if __name__ == "__main__":
    main()

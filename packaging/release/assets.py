"""Names shared by native packagers, release staging and signed build records."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
INSTALLERS = {
    "freebsd15": "crumbwriter-{version}-FreeBSD-15-amd64.pkg",
    "freebsd14": "crumbwriter-{version}-FreeBSD-14-amd64.pkg",
    "ubuntu24.04": "crumbwriter_{version}-ubuntu24.04_amd64.deb",
    "ubuntu26.04": "crumbwriter_{version}-ubuntu26.04_amd64.deb",
    "debian13": "crumbwriter_{version}-debian13_amd64.deb",
    "windows": "CrumbWriter-{version}-Windows-x64-Setup.exe",
    "macos": "CrumbWriter-{version}-macOS-Universal.dmg",
}


def project_version():
    header = (ROOT / "src/qt/buildinfo.h").read_text(encoding="utf-8")
    match = re.search(r'^inline constexpr char version\[\] = "(\d+\.\d+(?:\.\d+)?)";$',
                      header, re.MULTILINE)
    if not match:
        raise ValueError("Missing or invalid version in src/qt/buildinfo.h")
    return match.group(1)


def installers(version):
    return {target: name.format(version=version) for target, name in INSTALLERS.items()}


if __name__ == "__main__":
    version = project_version()
    targets = installers(version)
    command = sys.argv[1:]
    if command == ["version"]:
        print(version)
    elif command == ["all"]:
        print(*targets.values(), sep="\n")
    elif len(command) == 3 and command[0] == "notes":
        repository, tag = command[1:]
        if tag != f"v{version}":
            sys.exit("Release tag and application version differ")
        notes = Path(__file__).with_name("notes.md").read_text(encoding="utf-8")
        changelog = Path(__file__).with_name("changes") / f"{version}.md"
        changes = changelog.read_text(encoding="utf-8").strip() if changelog.is_file() else ""
        for key, value in dict(targets, REPOSITORY=repository, TAG=tag, CHANGES=changes).items():
            notes = notes.replace(f"@{key}@", value)
        if re.search(r"@[a-zA-Z0-9.]+@", notes):
            sys.exit("Unknown placeholder in release notes")
        print(notes.lstrip(), end="")
    elif len(command) == 1 and command[0] in targets:
        print(targets[command[0]])
    else:
        sys.exit("Usage: assets.py {version|all|" + "|".join(targets) + "}\n"
                 "       assets.py notes REPOSITORY TAG")

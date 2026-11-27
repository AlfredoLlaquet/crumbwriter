"""Identify the actual DLLs selected by the Windows packager from pacman's database."""

from collections import defaultdict
from pathlib import PurePosixPath
from urllib.error import HTTPError

from source_files import Notices, archive_notices, download, notice_filename


def pacman_fields(path):
    result = {}
    for block in path.read_text(encoding="utf-8").strip().split("\n\n"):
        if not block.strip():
            continue
        header, *values = block.strip().splitlines()
        result[header.strip("%")] = values
    return result


def stage_msys(destination, prefix, sources):
    prefix = prefix.resolve()
    root = prefix.parent
    metadata = root / "var/lib/pacman/local"
    if not metadata.is_dir():
        raise RuntimeError(f"Missing MSYS2 package database under {root}")

    # The same owner may install several Qt plugins with different subdirectories.
    needed = {item.name.casefold() for item in destination.rglob("*") if item.suffix.lower() == ".dll" or item.name.lower() == "pdftotext.exe"}
    data_paths = {file.relative_to(destination).as_posix() for file in (destination / "share/poppler").rglob("*") if file.is_file()}
    if not data_paths:
        raise RuntimeError("Missing Poppler encoding data in the Windows stage")
    if not needed:
        raise RuntimeError("The Windows stage has no runtime DLLs")
    packages = defaultdict(list)
    files = defaultdict(set)
    for directory in sorted(metadata.iterdir()):
        if not (directory / "files").is_file():
            continue
        listing = pacman_fields(directory / "files").get("FILES", [])
        owned = [
            f for f in listing if f.startswith(prefix.name + "/")
            and PurePosixPath(f).name.casefold() in needed
        ]
        owned_data = [f for f in listing if f.removeprefix(prefix.name + "/") in data_paths]
        if not owned and not owned_data:
            continue
        fields = pacman_fields(directory / "desc")
        name = fields["NAME"][0]
        version = fields["VERSION"][0].split(":")[-1]
        base = fields.get("BASE", fields["NAME"])[0]
        # Some versions of pacman omit the base field for a non-split package.
        base = base.replace("mingw-w64-ucrt-x86_64-", "mingw-w64-", 1)
        key = (base, version)
        packages[key].append((name, listing))
        files[key].update(PurePosixPath(f).name.casefold() for f in owned)
        files[key].update(f.removeprefix(prefix.name + '/') for f in owned_data)
    needed.update(data_paths)
    found = set().union(*files.values())
    if needed - found:
        raise RuntimeError("Unidentified staged runtime files: " + ", ".join(sorted(needed - found)))

    lines = []
    for (base, version), pkgs in sorted(packages.items()):
        key = f"{base}-{version}"
        notices = Notices(destination / "third-party" / key)
        for _, listing in pkgs:
            for file in listing:
                if "/share/licenses/" in file or ("/share/doc/" in file and notice_filename(PurePosixPath(file).name)):
                    local = root / file
                    if local.is_file():
                        notices.add(file, local.read_bytes())
        # The distributor's source archives contain both its recipe/patches and upstream source.
        for ext in ("zst", "xz", "gz"):
            filename = f"{key}.src.tar.{ext}"
            url = "https://repo.msys2.org/mingw/sources/" + filename
            try:
                download(url, sources / filename)
                break
            except HTTPError as error:
                if error.code != 404 or ext == "gz":
                    raise
        if not notices.index or base == "mingw-w64-qt6-base":
            archive_notices(sources / filename, notices, nested=True)
        notices.finish()
        names = ", ".join(sorted(n for n, _ in pkgs))
        lines.append(f"{names}  {version}\n  {url}\n  {', '.join(sorted(files[(base, version)]))}")
    return lines

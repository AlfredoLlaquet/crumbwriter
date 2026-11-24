"""Prepare the dictionaries and third-party notices for a native installer."""

import argparse
from pathlib import Path
import tempfile
import zipfile

from brew_sources import stage_brew, stage_qt
from msys_sources import stage_msys
from source_files import download, source_index
from thesaurus import stage_thesaurus

DICTIONARY = "hunspell-en_US-2026.02.25.zip"
DICTIONARY_URL = "https://github.com/en-wl/wordlist/releases/download/rel-2026.02.25/" + DICTIONARY
DICTIONARY_SHA256 = "ac8e73310e951d88c52c2cf2ba54ceaca34f8486a81630ac8a75dc5f931179f9"


def stage_dictionary(destination, source_dir):
    archive = source_dir / DICTIONARY
    download(DICTIONARY_URL, archive, DICTIONARY_SHA256)
    dictionary_dir = destination / "dictionaries"
    notice_dir = destination / "third-party/SCOWL"
    dictionary_dir.mkdir(parents=True, exist_ok=True)
    notice_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        for name in ("en_US.aff", "en_US.dic"):
            (dictionary_dir / name).write_bytes(package.read(name))
        (notice_dir / "README_en_US.txt").write_bytes(package.read("README_en_US.txt"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=Path, help="Windows installation root or macOS Contents/Resources")
    parser.add_argument("--msys-root", type=Path, help="installed UCRT64 prefix")
    parser.add_argument("--homebrew-runtime-list", type=Path, help="original absolute paths of deployed Homebrew dylibs")
    parser.add_argument("--qt-prefix", type=Path, help="directory containing the Qt for macOS SDK")
    parser.add_argument("--sources", type=Path, help="destination for source archives to publish with this binary")
    args = parser.parse_args()
    if bool(args.homebrew_runtime_list) != bool(args.qt_prefix):
        parser.error("the macOS build needs both --homebrew-runtime-list and --qt-prefix")
    if args.msys_root and args.qt_prefix:
        parser.error("select one target platform")
    if (args.qt_prefix or args.msys_root) and not args.sources:
        parser.error("native package builds require --sources")

    with tempfile.TemporaryDirectory() as tmp:
        sources = args.sources or Path(tmp)
        sources.mkdir(parents=True, exist_ok=True)
        stage_dictionary(args.destination, sources)
        entries = [f"SCOWL American English dictionary  2026.02.25\n  {DICTIONARY_URL}"]
        entries.append(stage_thesaurus(args.destination, sources))
        if args.msys_root:
            entries += stage_msys(args.destination, args.msys_root, sources)
        if args.qt_prefix:
            entries.append(stage_qt(args.destination, args.qt_prefix, sources))
            entries += stage_brew(args.destination, args.homebrew_runtime_list, sources)
        lines = """Crumb Writer uses third-party software

The Unlicense covers Crumb Writer's original code. The libraries, American
English spelling dictionary and thesaurus retain their respective licenses.
Component directories here contain the copyright, license, and attribution
notices provided by their authors. Qt libraries are dynamically linked under
the GNU Lesser General Public License, version 3. The licenses permit replacing
the LGPL libraries and debugging modifications to them.

Matching source archives are available beside this installer on the GitHub
Release page for this version. Each source download contains a SHA256SUMS.txt
index for its contents, including the distribution's build recipes when used.
The public application source includes the scripts for rebuilding the editor
and creating an installer with replacement libraries.

https://github.com/AlfredoLlaquet/crumbwriter/releases

"""
        (args.destination / "third-party/README.txt").write_text(lines, encoding="utf-8")
        catalog = "\n\n".join(entries) + "\n"
        (args.destination / "third-party/sources.txt").write_text(catalog, encoding="utf-8")
        if args.sources:
            (sources / "sources.txt").write_text(catalog, encoding="utf-8")
            source_index(sources)


if __name__ == "__main__":
    main()

"""Stage the pinned MyThes US English dictionary, index, and redistribution notices."""

import argparse
from pathlib import Path
import tarfile
import tempfile

from source_files import download, sha256

REVISION = "dd077939ef2cf3a1a866e3d40ae7c6d5cc78abbb"
ARCHIVE = f"mythes-{REVISION}.tar.gz"
URL = f"https://codeload.github.com/hunspell/mythes/tar.gz/{REVISION}"
SHA256 = "b43201e781b11a2bd36b69b0cf9970ba76a83ac7051fd4549390c75b9aab327b"


def stage_thesaurus(destination, sources):
    archive = sources / ARCHIVE
    if not archive.is_file() or sha256(archive) != SHA256:
        download(URL, archive, SHA256)
    contents = {
        "th_en_US_new.dat": "thesaurus/th_en_US_new.dat",
        "th_en_US_new.idx": "thesaurus/th_en_US_new.idx",
        "COPYING": "third-party/MyThes/COPYING",
        "WordNet_license.txt": "third-party/MyThes/WordNet_license.txt",
        "WordNet_readme.txt": "third-party/MyThes/WordNet_readme.txt",
    }
    with tarfile.open(archive, "r:gz") as package:
        for name, relative_path in contents.items():
            target = destination / relative_path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(package.extractfile(f"mythes-{REVISION}/{name}").read())
    return f"MyThes engine and WordNet American English thesaurus  {REVISION}\n  {URL}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True, help="package data root")
    parser.add_argument("--sources", type=Path, help="keep the source archive in this directory")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        print(stage_thesaurus(args.destination, args.sources or Path(temporary)))

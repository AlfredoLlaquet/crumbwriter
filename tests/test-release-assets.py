"""Check release downloads, the README links, source parts and their signed checksum list."""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "packaging/release"))
import assets
import prepare


class ReleaseAssets(unittest.TestCase):
    def setUp(self):
        self.version = assets.project_version()
        self.names = assets.installers(self.version)

    def test_download_links(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        notes = subprocess.check_output([sys.executable, assets.__file__, "notes", "owner/project",
                                         f"v{self.version}"], text=True)
        for name in self.names.values():
            with self.subTest(name=name):
                link = f"/releases/download/v{self.version}/{name})"
                self.assertIn(link, readme)
                self.assertIn(link, notes)

    def test_packaged_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            incoming, dist = base / "incoming", base / "dist"
            expected = {}
            for i, (target, name) in enumerate(self.names.items()):
                artifact = incoming / f"{target}-installer"
                artifact.mkdir(parents=True)
                expected[name] = bytes([i])
                (artifact / name).write_bytes(expected[name])
            for artifact, template in prepare.SOURCE_PACKAGES.items():
                name = template.format(version=self.version)
                folder = incoming / artifact
                folder.mkdir()
                (folder / name).write_bytes(b"123456789")
                expected[f"{name}.part001"] = b"12345678"
                expected[f"{name}.part002"] = b"9"
            with patch.object(prepare, "PART_SIZE", 8):
                prepare.collect(incoming, dist)
            self.assertEqual(set(path.name for path in dist.iterdir()), {*expected, "SHA256SUMS"})
            hashes = []
            for name, data in sorted(expected.items()):
                self.assertEqual((dist / name).read_bytes(), data)
                hashes.append(f"{hashlib.sha256(data).hexdigest()}  {name}\n")
            self.assertEqual((dist / "SHA256SUMS").read_text(encoding="ascii"), "".join(hashes))


if __name__ == "__main__":
    unittest.main()

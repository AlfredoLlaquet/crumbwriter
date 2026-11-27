"""Copy the Qt plugins and UCRT64 DLLs required by the installed Windows editor."""

import argparse
from collections import deque
from pathlib import Path
import os
import re
import shutil
import subprocess


def deploy(destination, prefix):
    executable = destination / "crumbwriter.exe"
    if not executable.is_file():
        raise FileNotFoundError(executable)
    bin_dir = prefix / "bin"
    libraries = {p.name.casefold(): p for p in bin_dir.glob("*.dll")}
    system_dir = Path(os.environ["SystemRoot"]) / "System32"
    converter = destination / "pdftotext.exe"
    shutil.copy2(bin_dir / converter.name, converter)
    shutil.copytree(prefix / 'share/poppler', destination / 'share/poppler', dirs_exist_ok=True)
    pending = deque([executable, converter])
    plugin_dir = prefix / "share" / "qt6" / "plugins"
    for name in ("platforms/qwindows.dll", "styles/qmodernwindowsstyle.dll"):
        plugin = destination / "plugins" / name
        plugin.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(plugin_dir / name, plugin)
        pending.append(plugin)

    # Qt resolves plugins relative to the installed exe even on a machine with Qt SDKs.
    (destination / "qt.conf").write_text("[Paths]\nPlugins=plugins\n", encoding="ascii")
    seen = set()
    while pending:
        binary = pending.popleft()
        output = subprocess.check_output([bin_dir / "objdump.exe", "-p", binary],
                                         encoding="utf-8", errors="replace")
        for imported in re.findall(r"^\s*DLL Name:\s*(\S+)\s*$", output, re.MULTILINE):
            key = imported.casefold()
            if key in seen:
                continue
            seen.add(key)
            source = libraries.get(key)
            if source:
                target = destination / source.name
                shutil.copy2(source, target)
                pending.append(target)
            elif not (system_dir / imported).is_file() and not key.startswith(("api-ms-", "ext-ms-")):
                raise RuntimeError(f"Missing DLL {imported} imported by {binary.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("prefix", type=Path)
    options = parser.parse_args()
    deploy(options.destination.resolve(), options.prefix.resolve())

"""Assemble, merge, and inspect Crumb Writer's relocatable macOS app bundle."""

import argparse
import filecmp
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tempfile


CPU_FAMILIES = ("arm64", "x86_64")
MACH_MAGIC = {bytes.fromhex(magic) for magic in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe",
    "cafebabe", "bebafeca", "cafebabf", "bfbafeca",
)}
LINK_COMMANDS = {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"}
SYSTEM_ROOTS = ("/System/Library/", "/usr/lib/")


def run(*args):
    subprocess.run(args, check=True)


def output(*args):
    return subprocess.check_output(args, text=True)


def app_executable(app):
    info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    return app / "Contents/MacOS" / info["CFBundleExecutable"]


def mach_files(app):
    result = set()
    for path in app.rglob("*"):
        if not path.is_symlink() and path.is_file():
            with path.open("rb") as binary:
                if binary.read(4) in MACH_MAGIC:
                    result.add(path.relative_to(app))
    return result


def load_commands(binary, cpu):
    """Read actual load commands, excluding the dylib's own install name."""
    libraries, rpaths = [], []
    command = None
    for line in output("otool", "-arch", cpu, "-l", binary).splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line.split()[1]
        elif line.startswith("name ") and command in LINK_COMMANDS:
            libraries.append(line[5:].rsplit(" (offset ", 1)[0])
        elif line.startswith("path ") and command == "LC_RPATH":
            rpaths.append(line[5:].rsplit(" (offset ", 1)[0])
    return libraries, rpaths


def expand_loader(name, binary, main):
    for token, folder in (("@loader_path", binary.parent), ("@executable_path", main.parent)):
        if name == token or name.startswith(token + "/"):
            return folder / name[len(token):].lstrip("/")
    return Path(name)


def relocate_homebrew(app, record, cpu):
    """Copy only dylibs the editor loads, and record their exact Cellar origins."""
    main = app_executable(app)
    cellar = Path(output("brew", "--cellar").strip()).resolve()
    frameworks = app / "Contents/Frameworks"
    frameworks.mkdir(parents=True, exist_ok=True)
    originals = {}
    install_names = {}
    pending = [(main, main)]
    while pending:
        original, staged = pending.pop()
        libraries, rpaths = load_commands(original, cpu)
        edits = []
        for name in libraries:
            if name.startswith("@rpath/"):
                candidates = [expand_loader(rpath, original, main) / name[7:] for rpath in rpaths]
            else:
                candidates = [expand_loader(name, original, main)]
            source = next((p.resolve() for p in candidates
                           if p.is_file() and p.resolve().is_relative_to(cellar)), None)
            if source is None:
                continue
            if source not in install_names:
                # Use the library's ABI install name, shared across compatible
                # patch releases, while retaining the exact source keg for each CPU.
                dylib_id = output("otool", "-arch", cpu, "-D", source).strip().splitlines()[-1]
                install_names[source] = Path(dylib_id).name
            library_name = install_names[source]
            if not library_name.endswith(".dylib"):
                raise ValueError(f"Invalid dylib install name for {source}: {library_name}")
            if library_name in originals and originals[library_name] != source:
                raise ValueError(f"Conflicting package libraries: {originals[library_name]} and {source}")
            if library_name not in originals:
                originals[library_name] = source
                target = frameworks / library_name
                shutil.copy2(source, target)
                target.chmod(0o755)
                pending.append((source, target))
            edits += ["-change", name, "@executable_path/../Frameworks/" + library_name]
        if original != main:
            edits += ["-id", "@rpath/" + staged.name]
        if edits:
            run("install_name_tool", *edits, staged)

    for expected in ("libglib-", "libhunspell-", "libarchive."):
        if not any(name.startswith(expected) for name in originals):
            raise ValueError(f"Missing Homebrew runtime dependency: {expected}")
    record.write_text("".join(str(p) + "\n" for p in sorted(originals.values())), encoding="utf-8")


def prepare(app, qt, record):
    cpu = platform.machine()
    relocate_homebrew(app, record, cpu)
    plugins = app / "Contents/PlugIns"
    for name in ("platforms/libqcocoa.dylib", "styles/libqmacstyle.dylib"):
        target = plugins / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(qt / "plugins" / name, target)
    run(qt / "bin/macdeployqt", app, "-no-plugins", "-no-strip", "-verbose=1")
    (app / "Contents/Resources/qt.conf").write_text("[Paths]\nPlugins = PlugIns\n", encoding="ascii")

    # CMake can remove every build rpath during install. macdeployqt rewrites
    # existing rpaths, so an installed executable can still need its first one.
    main = app_executable(app)
    _, main_paths = load_commands(main, cpu)
    if "@executable_path/../Frameworks" not in main_paths:
        run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", main)

    # Discard any search path to a build machine. dyld may use only bundle or OS libraries.
    for relative in sorted(mach_files(app)):
        binary = app / relative
        _, rpaths = load_commands(binary, cpu)
        for rpath in set(rpaths):
            if rpath.startswith("/") and not rpath.startswith(SYSTEM_ROOTS):
                run("install_name_tool", "-delete_rpath", rpath, binary)


def sign(app):
    for relative in sorted(mach_files(app)):
        run("codesign", "--force", "--sign", "-", "--timestamp=none", app / relative)
    bundles = [p for p in app.rglob("*") if p.is_dir() and not p.is_symlink()
               and p.suffix in {".framework", ".app", ".xpc", ".bundle"}]
    for bundle in sorted(bundles, key=lambda p: len(p.parts), reverse=True):
        if bundle.suffix != ".bundle" or mach_files(bundle):
            run("codesign", "--force", "--sign", "-", "--timestamp=none", bundle)
    run("codesign", "--force", "--sign", "-", "--timestamp=none", app)


def combine_notices(arm, intel, merged):
    folder = Path("Contents/Resources/third-party")
    for path in (intel / folder).rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(intel / folder)
        destination = merged / folder / relative
        if destination.is_file() and not filecmp.cmp(destination, path, shallow=False):
            # Distinct receipts can legitimately accompany each architecture.
            for cpu, tree in (("arm64", arm), ("x86_64", intel)):
                entry = merged / folder / cpu / relative
                entry.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(tree / folder / relative, entry)
            destination.unlink()
        elif not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)


def merge(arm, intel, destination):
    native = {"arm64": arm, "x86_64": intel}
    arm_files, intel_files = mach_files(arm), mach_files(intel)
    if arm_files != intel_files:
        raise ValueError(f"The CPU packages contain different code: {sorted(arm_files ^ intel_files)}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    run("ditto", arm, destination)
    with tempfile.TemporaryDirectory() as tmp:
        for relative in sorted(arm_files):
            slices = []
            for cpu, tree in native.items():
                path = tree / relative
                archs = output("lipo", "-archs", path).split()
                if archs != [cpu]:
                    thin = Path(tmp) / cpu
                    run("lipo", path, "-thin", cpu, "-output", thin)
                    path = thin
                slices.append(path)
            binary = destination / relative
            run("lipo", "-create", *slices, "-output", binary)
            binary.chmod(0o755)
    combine_notices(arm, intel, destination)
    sign(destination)
    verify(destination, CPU_FAMILIES)


def verify(app, cpus):
    main = app_executable(app)
    contents = app / "Contents"
    for entry in ("Resources/LICENSE", "Resources/CrumbWriter.txt", "Resources/CrumbWriter.icns",
                  "Resources/dictionaries/en_US.aff", "Resources/dictionaries/en_US.dic",
                  "Resources/thesaurus/th_en_US_new.dat", "Resources/thesaurus/th_en_US_new.idx",
                  "Resources/third-party/MyThes/WordNet_license.txt",
                  "Resources/third-party", "PlugIns/platforms/libqcocoa.dylib"):
        if not (contents / entry).exists():
            raise ValueError(f"Missing packaged resource: {entry}")
    files = mach_files(app)
    if main.relative_to(app) not in files:
        raise ValueError("Missing Mach-O application executable")
    for cpu in cpus:
        _, rpaths = load_commands(main, cpu)
        if "@executable_path/../Frameworks" not in rpaths:
            raise ValueError(f"{cpu} executable has no run path to its packaged frameworks")
    for rel in sorted(files):
        binary = app / rel
        slices = set(output("lipo", "-archs", binary).split())
        if not set(cpus) <= slices:
            raise ValueError(f"{rel} has {slices}, requires {cpus}")
        for cpu in cpus:
            libraries, rpaths = load_commands(binary, cpu)
            for rpath in rpaths:
                if rpath.startswith(SYSTEM_ROOTS):
                    continue
                path = expand_loader(rpath, binary, main)
                if not path.is_absolute() or not path.resolve().is_relative_to(app):
                    raise ValueError(f"{binary} has an external search path: {rpath}")
            for name in libraries:
                if name.startswith(SYSTEM_ROOTS):
                    continue
                if name.startswith("@rpath/"):
                    path = contents / "Frameworks" / name[7:]
                else:
                    path = expand_loader(name, binary, main)
                if not path.is_file() or not path.resolve().is_relative_to(app):
                    raise ValueError(f"{binary} loads an external or missing library: {name}")
    run("codesign", "--verify", "--deep", "--strict", "--all-architectures", "--verbose=2", app)
    print(f"Verified {len(files)} Mach-O files for {', '.join(cpus)}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, paths in (("prepare", ("app", "qt", "record")), ("sign", ("app",)),
                        ("merge", ("arm", "intel", "destination")), ("verify", ("app",))):
        command = commands.add_parser(name)
        for path in paths:
            command.add_argument(path, type=lambda arg: Path(arg).resolve())
        if name == "verify":
            command.add_argument("cpus", nargs="+", choices=CPU_FAMILIES)
    options = vars(parser.parse_args())
    operation = options.pop("command")
    operations = {"prepare": prepare, "merge": merge, "sign": sign, "verify": verify}
    operations[operation](**options)


if __name__ == "__main__":
    main()

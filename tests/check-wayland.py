#!/usr/bin/env python3
"""Type into the native Wayland editor through Weston's outer X11 keyboard."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def check(app, run):
    env = dict(os.environ, LC_ALL="C.UTF-8", GSETTINGS_BACKEND="memory", NO_AT_BRIDGE="1")
    for name in ("config", "data", "state", "cache", "runtime"):
        folder = run / name
        folder.mkdir(mode=0o700)
        env["XDG_" + name.upper() + ("_DIR" if name == "runtime" else "_HOME")] = str(folder)

    manuscript = run / "Typing.txt"
    first = "Crumb 1: First: Original.\n"
    second = "Crumb 2: Second: "
    manuscript.write_bytes((first + second).encode())
    compositor_log = run / "weston.log"
    client_log = run / "client.log"
    processes = []

    def wait_for(description, condition):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            for process in processes:
                if process.poll() is not None:
                    raise AssertionError(f"{process.args[0]} exited with {process.returncode} before {description}")
            result = condition()
            if result:
                return result
            time.sleep(0.08)
        raise AssertionError("Timed out waiting for " + description)

    def outer_window():
        # The X11 backend reports its output XID even without a window manager.
        match = re.search(r"x11 output \d+x\d+, window id (\d+)", compositor_log.read_text(errors="replace"))
        return match[1] if match else None

    def keyboard(*args):
        # No --window here: xdotool sends XTEST input to the focused compositor.
        subprocess.run(["xdotool", *args], check=True, timeout=8)

    def save_and_check(expected):
        keyboard("key", "--clearmodifiers", "ctrl+s")
        wait_for(f"saved UTF-8 bytes {expected!r}", lambda: manuscript.read_bytes() == expected.encode("utf-8"))

    try:
        with compositor_log.open("w") as log:
            weston = subprocess.Popen(["weston", "--no-config", "--backend=x11", "--renderer=pixman",
                                       "--shell=kiosk", "--width=1040", "--height=720", "--idle-time=0",
                                       "--socket=crumb-test"], env=env, stdout=log, stderr=subprocess.STDOUT)
        processes.append(weston)
        wait_for("Weston's private Wayland socket", (run / "runtime" / "crumb-test").is_socket)
        window = wait_for("Weston's X11 output ID", outer_window)
        keyboard("windowmap", "--sync", window)
        keyboard("windowfocus", "--sync", window)

        client_env = dict(env, GDK_BACKEND="wayland", WAYLAND_DISPLAY="crumb-test",
                          GTK_IM_MODULE="wayland", WAYLAND_DEBUG="client")
        client_env.pop("DISPLAY", None)
        with client_log.open("w") as log:
            editor = subprocess.Popen([app, manuscript], env=client_env, stdout=log, stderr=subprocess.STDOUT)
        processes.append(editor)
        wait_for("the editor's first Wayland buffer", lambda: re.search(r"wl_surface[^\s(]*\.attach\(wl_buffer",
                                                                       client_log.read_text(errors="replace")))
        wait_for("the Crumb Writer desktop ID", lambda: re.search(
            r'xdg_toplevel[^\s(]*\.set_app_id\("org\.thecrumbs\.CrumbWriter"\)', client_log.read_text(errors="replace")))
        print("Wayland window identifies itself with the Crumb Writer desktop file.", flush=True)
        keyboard("mousemove", "--window", window, "250", "240", "click", "1")
        wait_for("Wayland keyboard focus", lambda: re.search(r"\bwl_keyboard[^\s(]*\.enter\(",
                                                            client_log.read_text(errors="replace")))

        keyboard("key", "--clearmodifiers", "ctrl+Next")
        keyboard("type", "--clearmodifiers", "--delay", "14", "I'd say \"Pepe,\" new--s ... next.")
        typed = "I’d say “Pepe,” new—s … next."
        save_and_check(first + second + typed)
        print("Wayland keyboard converted apostrophe, quotes, dash, and ellipsis.", flush=True)

        keyboard("key", "--clearmodifiers", "--delay", "60", "ctrl+Prior", "ctrl+Down", "Tab", "Tab", "shift+Tab")
        keyboard("type", "--clearmodifiers", "Verified ")
        verified = first + "Crumb 2: Verified Second: " + typed
        save_and_check(verified)
        print("Wayland Ctrl+Down, Tab, and Shift+Tab reached the intended fields.", flush=True)

        keyboard("key", "--clearmodifiers", "--delay", "60", "ctrl+Next", "Down", "Down", "ctrl+Return")
        keyboard("type", "--clearmodifiers", "V")
        save_and_check(verified + "\n\nV")
        keyboard("key", "--clearmodifiers", "ctrl+z")
        save_and_check(verified)
        print("Wayland virtual typing materialized two newlines and one undo restored the original bytes.", flush=True)
        keyboard("key", "--clearmodifiers", "ctrl+w")
        if editor.wait(timeout=8) != 0:
            raise AssertionError("Closing the editor failed")
    except BaseException:
        print("Manuscript bytes:", repr(manuscript.read_bytes()), file=sys.stderr)
        for log in (compositor_log, client_log):
            if log.exists():
                print(f"--- {log.name} ---\n{log.read_text(errors='replace')[-7000:]}", file=sys.stderr)
        raise
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: xvfb-run -a dbus-run-session -- python3 tests/check-wayland.py ./crumbwriter")
    with tempfile.TemporaryDirectory(prefix="crumbwriter-wayland-") as directory:
        check(Path(sys.argv[1]).resolve(), Path(directory))

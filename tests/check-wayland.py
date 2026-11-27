#!/usr/bin/env python3
"""Type into the native Wayland editor through Weston's outer X11 keyboard."""

import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time


def check(app, run):
    env = dict(os.environ, LC_ALL="C.UTF-8")
    for name in ("config", "data", "state", "cache", "runtime"):
        folder = run / name
        folder.mkdir(mode=0o700)
        env["XDG_" + name.upper() + ("_DIR" if name == "runtime" else "_HOME")] = str(folder)

    manuscript = run / "Typing.txt"
    notes = run / "Reading notes.txt"
    notes.write_text("A second manuscript in the same Wayland window.\n", encoding="utf-8")
    first = "Notes//Crumb 1: First: Original.\n"
    second = "Crumb 2: Second:"
    manuscript.write_bytes((first + second).encode())
    compositor_log = run / "weston.log"
    client_log = run / "client.log"
    processes = []
    compositor_fd, client_fd = socket.socketpair()

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

    def active_toplevel(file):
        log = client_log.read_text(errors="replace")
        titles = re.findall(r'\b(xdg_toplevel[^\s(]*)\.set_title\("([^"]*)"\)', log)
        if titles and titles[-1][1] == file.name + " — Crumb Writer":
            return titles[-1][0]
        return None

    def save_and_check(expected):
        expected += "\n"
        keyboard("key", "--clearmodifiers", "ctrl+s")
        wait_for(f"saved UTF-8 bytes {expected!r}", lambda: manuscript.read_bytes() == expected.encode("utf-8"))
        wait_for("the window to acknowledge the completed save", lambda: active_toplevel(manuscript))

    try:
        # Give the compositor and this one client a private Wayland connection.
        server_env = dict(env, WAYLAND_SERVER_SOCKET=str(compositor_fd.fileno()))
        # X11 over TCP cannot use the pixman backend's shared-memory surfaces.
        renderer = "pixman" if os.environ["DISPLAY"].startswith(":") else "gl"
        with compositor_log.open("w") as log:
            weston = subprocess.Popen(["weston", "--no-config", "--backend=x11", "--renderer=" + renderer,
                                       "--shell=kiosk", "--width=1040", "--height=720", "--idle-time=0"],
                                      env=server_env, pass_fds=(compositor_fd.fileno(),),
                                      stdout=log, stderr=subprocess.STDOUT)
        processes.append(weston)
        compositor_fd.close()
        window = wait_for("Weston's X11 output ID", outer_window)
        keyboard("windowmap", "--sync", window)
        keyboard("windowfocus", "--sync", window)

        client_env = dict(env, QT_QPA_PLATFORM="wayland", WAYLAND_SOCKET=str(client_fd.fileno()),
                          WAYLAND_DEBUG="client")
        client_env.pop("DISPLAY", None)
        with client_log.open("w") as log:
            editor = subprocess.Popen([app, notes, manuscript], env=client_env, pass_fds=(client_fd.fileno(),),
                                      stdout=log, stderr=subprocess.STDOUT)
        processes.append(editor)
        client_fd.close()
        wait_for("the editor's first Wayland buffer", lambda: re.search(r"wl_surface[^\s(]*\.attach\(wl_buffer",
                                                                       client_log.read_text(errors="replace")))
        wait_for("the Crumb Writer desktop ID", lambda: re.search(
            r'xdg_toplevel[^\s(]*\.set_app_id\("org\.thecrumbs\.CrumbWriter"\)', client_log.read_text(errors="replace")))
        print("Wayland window identifies itself with the Crumb Writer desktop file.", flush=True)
        keyboard("mousemove", "--window", window, "520", "240", "click", "1")
        wait_for("Wayland keyboard focus", lambda: re.search(r"\bwl_keyboard[^\s(]*\.enter\(",
                                                            client_log.read_text(errors="replace")))
        writer = wait_for("the last file from the command line", lambda: active_toplevel(manuscript))
        keyboard("key", "--clearmodifiers", "ctrl+shift+Tab")
        wait_for("the other startup tab in the same native window", lambda: active_toplevel(notes) == writer)
        keyboard("key", "--clearmodifiers", "ctrl+Tab")
        wait_for("the writing tab in the original Wayland window", lambda: active_toplevel(manuscript) == writer)
        print("Both startup manuscripts use the same Wayland toplevel.", flush=True)

        keyboard("key", "--clearmodifiers", "ctrl+Next")
        keyboard("type", "--clearmodifiers", "--delay", "14", "  I'd say  \"Pepe,\" new--s ... next.  ")
        typed = " I’d say “Pepe,” new—s … next."
        wait_for("live save without trailing spaces",
                 lambda: manuscript.read_bytes() == (first + second + typed + "\n").encode())
        keyboard("type", "--clearmodifiers", "Another word.")
        typed += " Another word."
        save_and_check(first + second + typed)
        print("Wayland typing converted punctuation and kept one word separator across live save.", flush=True)

        keyboard("type", "--clearmodifiers", " fox")
        save_and_check(first + second + typed + " fox")
        keyboard("key", "--clearmodifiers", "--repeat", "3", "--delay", "60", "BackSpace")
        wait_for("live save after erasing the last word",
                 lambda: manuscript.read_bytes() == (first + second + typed + "\n").encode())
        keyboard("type", "--clearmodifiers", "panda")
        save_and_check(first + second + typed + " panda")
        keyboard("key", "--clearmodifiers", "--delay", "60", "Left", "Left", "ctrl+w")
        save_and_check(first + second + typed)
        print("Wayland backspacing preserved the separator across autosave; Ctrl+W deleted the whole word.", flush=True)

        # Cross a crumb boundary both ways: first title, first body, second title.
        keyboard("key", "--clearmodifiers", "--delay", "60", "ctrl+Prior", "Tab", "Tab", "Tab", "shift+Tab", "Tab")
        keyboard("type", "--clearmodifiers", "Verified ")
        verified = first + "Crumb 2: Verified Second:" + typed
        save_and_check(verified)
        print("Wayland Tab and Shift+Tab crossed crumbs through titles and bodies.", flush=True)

        keyboard("key", "--clearmodifiers", "ctrl+Prior", "ctrl+space")
        keyboard("type", "--clearmodifiers", "qvErIfIeD")
        keyboard("key", "--clearmodifiers", "Return")
        keyboard("type", "--clearmodifiers", "owls")
        save_and_check("owls" + verified.split("Verified", 1)[1])
        keyboard("key", "--clearmodifiers", "ctrl+z")
        save_and_check(verified)
        print("Wayland phrase selection crossed crumbs. One undo restored the passage after four typed letters.", flush=True)

        keyboard("key", "--clearmodifiers", "--delay", "60", "ctrl+Next", "Down", "Down", "ctrl+Return")
        keyboard("type", "--clearmodifiers", "V")
        save_and_check(verified + "\n\nV")
        keyboard("key", "--clearmodifiers", "ctrl+z")
        save_and_check(verified)
        print("Wayland virtual typing materialized two newlines and one undo restored the original bytes.", flush=True)

        keyboard("key", "--clearmodifiers", "ctrl+a")
        for row, text in enumerate(("Tide Tables", "", "The Moon", "-", "A goose counted.", "The moon waited.")):
            if row:
                keyboard("key", "--clearmodifiers", "Return")
            if text:
                keyboard("type", "--clearmodifiers", text)
        prose = "Tide Tables\n\nThe Moon\n--------\nA goose counted.\nThe moon waited."
        save_and_check(prose)
        keyboard("key", "--clearmodifiers", "ctrl+shift+4")
        keyboard("type", "--clearmodifiers", "moon")
        keyboard("key", "--clearmodifiers", "Return", "ctrl+space")
        keyboard("type", "--clearmodifiers", "j")  # The next visible paragraph follows the folded heading.
        keyboard("key", "--clearmodifiers", "Return")
        keyboard("type", "--clearmodifiers", "Lunar ")
        save_and_check(prose.replace("The Moon\n--------\n", "Lunar "))
        print("Generic mode navigated to a chapter by keyboard and accepted a two-paragraph movement.", flush=True)
        keyboard("key", "--clearmodifiers", "alt+w")
        if editor.wait(timeout=8) != 0:
            raise AssertionError("Closing the editor failed")
    except BaseException:
        print("Manuscript bytes:", repr(manuscript.read_bytes()), file=sys.stderr)
        for log in (compositor_log, client_log):
            if log.exists():
                print(f"--- {log.name} ---\n{log.read_text(errors='replace')[-7000:]}", file=sys.stderr)
        raise
    finally:
        compositor_fd.close()
        client_fd.close()
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

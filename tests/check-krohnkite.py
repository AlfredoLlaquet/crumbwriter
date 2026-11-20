#!/usr/bin/env python3
"""Exercise a built Crumb Writer and an unpacked Krohnkite package in an isolated KWin session."""

import itertools
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


def output(*args):
    return subprocess.check_output(args, text=True, timeout=8).strip()


def wait_for(description, check):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        try:
            result = check()
        except subprocess.CalledProcessError:
            result = None
        if result:
            return result
        time.sleep(0.1)
    raise AssertionError(f'{description}\nManaged windows:\n{output("wmctrl", "-lG")}')


def windows():
    result = {}
    for line in output('wmctrl', '-l').splitlines():
        fields = line.split(None, 3)
        if len(fields) == 4:
            result[fields[3]] = fields[0]
    return result


def rectangle(window):
    info = output('xwininfo', '-id', window)
    fields = ['Absolute upper-left X', 'Absolute upper-left Y', 'Width', 'Height']
    return tuple(int(re.search(field + r':\s*(-?\d+)', info)[1]) for field in fields)


def tiles(clients, screen):
    bounds = [rectangle(client) for client in clients]
    for (x, y, w, h), (x2, y2, w2, h2) in itertools.combinations(bounds, 2):
        if min(x+w, x2+w2) > max(x, x2) + 2 and min(y+h, y2+h2) > max(y, y2) + 2:
            return None
    # Decorations, XTerm character increments and configurable gaps leave some space.
    if sum(w*h for _, _, w, h in bounds) < .80 * screen[0] * screen[1]:
        return None
    if any(x < 0 or y < 0 or x+w > screen[0]+1 or y+h > screen[1]+1 for x, y, w, h in bounds):
        return None
    return bounds


def key(window, chord):
    output('xdotool', 'windowactivate', '--sync', window, 'key', '--clearmodifiers', chord)
    time.sleep(.3)


def session(app, run):
    procs = []
    log = run / 'session.log'
    with log.open('w') as stream:
        def launch(*args):
            proc = subprocess.Popen(args, stdout=stream, stderr=subprocess.STDOUT)
            procs.append(proc)
            return proc

        try:
            launch('kwin_x11')
            wait_for('KWin owns the X screen', lambda: 'KWin' in output('wmctrl', '-m'))
            wait_for('KWin loaded Krohnkite', lambda: 'true' in output(
                'gdbus', 'call', '--session', '--dest', 'org.kde.KWin', '--object-path', '/Scripting',
                '--method', 'org.kde.kwin.Scripting.isScriptLoaded', 'krohnkite'))

            first, second = run/'Alpha.txt', run/'Beta.txt'
            middle = 'Crumb 2: Middle: Copy the trailing spaces.  '
            original = f'Book//Crumb 1: First: An editor window.\n{middle}\nCrumb 3: Last: The end.\n'
            first.write_text(original)
            second.write_text('Crumb 1: Second: Another editor window.\n')
            screen = tuple(map(int, output('xdotool', 'getdisplaygeometry').split()))
            launch(app, first)
            first_title = 'Alpha.txt — Crumb Writer'
            alpha = wait_for('first document', lambda: windows().get(first_title))
            launch('xterm', '-title', 'Tile peer', '-e', 'sleep', '100')
            peer = wait_for('second application', lambda: windows().get('Tile peer'))
            hints = output('xprop', '-id', peer, 'WM_NORMAL_HINTS')
            cell = re.search(r'resize increment: (\d+) by (\d+)', hints)
            cell_width, cell_height = map(int, cell.groups())
            clients = [alpha, peer]
            initial = wait_for('two adjacent tiles fill the screen', lambda: tiles(clients, screen))
            print('KWin + Krohnkite, two clients:', initial, flush=True)

            def unchanged():
                actual = tiles(clients, screen)
                assert actual, 'tiles overlap or extend beyond the screen'
                for client, old, new in zip(clients, initial, actual):
                    if client == peer:
                        # KWin may offer the full tile before XTerm rounds to its character grid.
                        assert old[:2] == new[:2], (old, new)
                        assert abs(old[2]-new[2]) < cell_width and abs(old[3]-new[3]) < cell_height, (old, new)
                    else:
                        assert old == new, f'manuscript tile changed: {old} -> {new}'

            for chord in ('ctrl+f', 'Escape', 'ctrl+h', 'Escape'):
                key(alpha, chord)
                unchanged()
            for chord, name in [('ctrl+comma', 'Settings'), ('F1', 'Help')]:
                existing = set(windows().values())
                key(alpha, chord)

                def new_transient():
                    for candidate in set(windows().values()) - existing:
                        parent = output('xprop', '-id', candidate, 'WM_TRANSIENT_FOR')
                        if hex(int(alpha, 16)) in parent:
                            return candidate
                    return None

                dialog = wait_for(f'{name} is transient for the manuscript', new_transient)
                props = output('xprop', '-id', dialog, '_NET_WM_WINDOW_TYPE', '_NET_WM_STATE')
                assert '_NET_WM_WINDOW_TYPE_DIALOG' in props and '_NET_WM_STATE_MODAL' not in props, props
                unchanged()
                key(dialog, 'Escape')
                wait_for(f'{name} closed', lambda: dialog not in windows().values())

            expected = windows()
            subprocess.run([app, first], check=True, timeout=8)
            assert windows() == expected, 'reopening the same file created another window'
            unchanged()

            key(alpha, 'ctrl+Next')
            key(alpha, 'ctrl+Down')
            key(alpha, 'ctrl+q')
            copied = subprocess.check_output(['xclip', '-selection', 'clipboard', '-out'], timeout=5)
            assert copied == middle.encode(), f'clipboard bytes: {copied!r}'
            key(alpha, 'Escape')
            edited = original.replace(middle, middle + 'x')
            for chord, contents in [('x', edited), ('ctrl+y', original.replace(middle + '\n', '')),
                                    ('ctrl+z', edited), ('ctrl+z', original)]:
                key(alpha, chord)
                key(alpha, 'ctrl+s')
                assert first.read_bytes() == contents.encode(), f'wrong saved bytes after {chord}'

            subprocess.run([app, second], check=True, timeout=8)
            beta = wait_for('additional document', lambda: windows().get('Beta.txt — Crumb Writer'))
            clients.append(beta)
            initial = wait_for('three adjacent tiles fill the screen', lambda: tiles(clients, screen))
            print('KWin + Krohnkite, three clients:', initial, flush=True)
            short = min((alpha, beta), key=lambda win: rectangle(win)[3])
            assert rectangle(short)[3] < screen[1] * .7, 'the layout must exercise a short editor tile'
            for chord in ('ctrl+f', 'Escape', 'ctrl+h', 'Escape'):
                key(short, chord)
                unchanged()
            key(beta, 'ctrl+w')
            clients.remove(beta)
            wait_for('closing the extra manuscript retiles the others', lambda: tiles(clients, screen))
        except BaseException:
            print(log.read_text(errors='replace')[-16000:], file=sys.stderr)
            for title, window in windows().items():
                print(title, rectangle(window), output('xprop', '-id', window, '_NET_WM_STATE',
                      '_NET_WM_WINDOW_TYPE', 'WM_NORMAL_HINTS'), file=sys.stderr)
            raise
        finally:
            for proc in reversed(procs):
                if proc.poll() is None:
                    proc.terminate()
            for proc in procs:
                try:
                    proc.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    proc.kill()


def isolated(app, package):
    with tempfile.TemporaryDirectory(prefix='crumbwriter-kwin-') as tmp:
        run = Path(tmp)
        env = dict(os.environ, LC_ALL='C.UTF-8', GDK_BACKEND='x11', QT_QPA_PLATFORM='xcb',
                   KWIN_COMPOSE='N', QT_QUICK_BACKEND='software', NO_AT_BRIDGE='1', GSETTINGS_BACKEND='memory')
        for kind in ('config', 'data', 'state', 'cache', 'runtime'):
            subdir = run / kind
            subdir.mkdir(mode=0o700)
            env['XDG_' + kind.upper() + ('_DIR' if kind == 'runtime' else '_HOME')] = str(subdir)
        (run/'config'/'kwinrc').write_text(
            '[Plugins]\nkrohnkiteEnabled=true\n[Compositing]\nEnabled=false\n'
            '[Script-krohnkite]\nlogging=true\nlogNewWindows=true\nlogFilteredWindows=true\n'
            'logUnmanagedWindows=true\n')
        shutil.copytree(package, run/'data'/'kwin'/'scripts'/'krohnkite')
        subprocess.run(['dbus-run-session', '--', 'xvfb-run', '-a', '-s', '-screen 0 1000x760x24',
                        sys.executable, __file__, '--session', app, run], env=env, check=True, timeout=120)


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == '--session':
        session(Path(sys.argv[2]), Path(sys.argv[3]))
    elif len(sys.argv) == 3:
        isolated(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
    else:
        sys.exit('usage: check-krohnkite.py CRUMBWRITER UNPACKED_KROHNKITE_PACKAGE')

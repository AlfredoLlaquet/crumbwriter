#!/usr/bin/env python3
"""Exercise a built Crumb Writer and an unpacked Krohnkite package in an isolated KWin session."""

import itertools
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys
import tempfile
import time

INACTIVE_PAPER = (229, 229, 229)


def output(*args):
    return subprocess.check_output(args, text=True, timeout=8).strip()


def wait_for(description, check):
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        try:
            result = check()
        except subprocess.CalledProcessError:
            result = None
        if result:
            return result
        time.sleep(0.1)
    raise AssertionError(f'{description}\nManaged windows: {windows()}')


def windows():
    result = {}
    clients = output('xprop', '-root', '_NET_CLIENT_LIST')
    for window in re.findall(r'0x[0-9a-fA-F]+', clients):
        # A closing client may disappear between the two X11 requests.
        try:
            name = output('xdotool', 'getwindowname', window)
        except subprocess.CalledProcessError:
            continue
        result[name] = window
    return result


def kwin_running():
    manager = output('xprop', '-root', '_NET_SUPPORTING_WM_CHECK')
    window = re.search(r'0x[0-9a-fA-F]+', manager)
    return window and 'KWin' in output('xprop', '-id', window[0], '_NET_WM_NAME')


def rectangle(window):
    info = output('xwininfo', '-id', window)
    fields = ['Absolute upper-left X', 'Absolute upper-left Y', 'Width', 'Height']
    return tuple(int(re.search(field + r':\s*(-?\d+)', info)[1]) for field in fields)


def paper_color(window):
    x, y, width, height = rectangle(window)
    # The manuscript is the middle pane between the two sidebars.
    left, top = x + width // 2, y + height - 54
    return tuple(map(int, output(os.environ['CW_TEST_PAPER_SAMPLER'], str(left), str(top)).split()))


def expect_paper(window, expected, context):
    try:
        wait_for(context, lambda: paper_color(window) == expected)
    except AssertionError as error:
        raise AssertionError(f'{context}: rendered {paper_color(window)}, expected {expected}') from error


def restored_paper(window):
    color = paper_color(window)
    if color != INACTIVE_PAPER:
        time.sleep(.2)
        if paper_color(window) == color:
            return color
    return None


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


def clipboard_bytes():
    return subprocess.check_output(['xclip', '-selection', 'clipboard', '-out'], timeout=5)


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
            wait_for('KWin owns the X screen', kwin_running)
            wait_for('KWin loaded Krohnkite', lambda: 'true' in output(
                'gdbus', 'call', '--session', '--dest', 'org.kde.KWin', '--object-path', '/Scripting',
                '--method', 'org.kde.kwin.Scripting.isScriptLoaded', 'krohnkite'))

            first, second = run/'Alpha.txt', run/'Beta.txt'
            middle = 'Crumb 2: Middle: Copy this crumb.'
            original = f'Book//Crumb 1: First: An editor window.\n{middle}\nCrumb 3: Last: The end.\n'
            first.write_text(original.replace(middle, 'Crumb 2:\tMiddle: Copy \t this crumb.  '))
            second_text = 'Crumb 1: Second: Another editor window.'
            second.write_text(second_text + '\n')
            Path(str(second) + '.characters').write_text(
                'Antler, Reindeer\n"Captain, Widgets", Blue Cap, Pilot\n', encoding='utf-8')
            Path(str(second) + '.recurring').write_text(
                'Comets, Shooting Stars\nPlanet Pebble, Pebblepost\n', encoding='utf-8')
            screen = tuple(map(int, output('xdotool', 'getdisplaygeometry').split()))
            first_process = launch(app, first)
            first_title = 'Alpha.txt — Crumb Writer'
            alpha = wait_for('first document', lambda: windows().get(first_title))
            peer_process = launch('xterm', '-title', 'Tile peer', '-e', 'sleep', '240')
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

            output('xdotool', 'windowactivate', '--sync', peer)
            expect_paper(alpha, INACTIVE_PAPER, 'other application active')
            key(alpha, 'Escape')
            normal_paper = wait_for('reactivated manuscript returns to its theme color',
                                    lambda: restored_paper(alpha))
            for chord in ('F6', 'F7', 'F6', 'shift+F7', 'ctrl+p', 'e', 'Return', 'alt+Down', 'Escape',
                          'Escape', 'ctrl+r', 'ctrl+l', 'Escape', 'ctrl+h', 'Escape', 'F10', 'Escape'):
                key(alpha, chord)
                unchanged()
                expect_paper(alpha, normal_paper, f'main window after {chord}')
            for chord, name in [('ctrl+comma', 'Settings'), ('F1', 'Help'), ('alt+b', 'Bookmarks'),
                                ('ctrl+shift+i', 'Statistics')]:
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
                if name == 'Statistics':
                    key(dialog, 'ctrl+c')
                    expected_stats = 'Crumbs: 3\nTotal words: 18\nAverage words per crumb: 6.00'
                    assert clipboard_bytes().decode() == expected_stats
                key(dialog, 'Escape')
                wait_for(f'{name} closed', lambda: dialog not in windows().values())

            expected = windows()
            subprocess.run([app, first], check=True, timeout=8)
            assert windows() == expected, 'reopening the same file created another window'
            unchanged()

            key(alpha, 'ctrl+Prior')
            key(alpha, 'ctrl+Down')
            key(alpha, 'ctrl+1')
            copied = clipboard_bytes()
            assert copied == b'crumb 2', f'lowercase header clipboard bytes: {copied!r}'
            key(alpha, 'ctrl+q')
            copied = clipboard_bytes()
            assert copied == middle.encode(), f'clipboard bytes: {copied!r}'
            key(alpha, 'Escape')
            edited = original.replace(middle, middle + 'x')
            for chord, contents in [('x', edited), ('ctrl+y', original.replace(middle + '\n', '')),
                                    ('ctrl+z', edited), ('ctrl+z', original)]:
                key(alpha, chord)
                key(alpha, 'ctrl+s')
                wait_for(f'saved bytes after {chord}', lambda: first.read_bytes() == contents.encode())

            key(alpha, 'ctrl+End')
            subprocess.run(['xclip', '-selection', 'clipboard', '-in'], input=b'  Pasted\tclean\t \twords.  ',
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=5)
            key(alpha, 'ctrl+v')
            key(alpha, 'ctrl+q')
            copied = clipboard_bytes()
            pasted = middle + ' Pasted clean words.'
            assert copied == pasted.encode(), f'clipboard after paste: {copied!r}'
            key(alpha, 'ctrl+s')
            wait_for('saved bytes after paste', lambda: first.read_bytes() == original.replace(middle, pasted).encode())

            subprocess.run([app, second], check=True, timeout=8)
            beta = wait_for('additional document', lambda: windows().get('Beta.txt — Crumb Writer'))
            clients.append(beta)
            initial = wait_for('three adjacent tiles fill the screen', lambda: tiles(clients, screen))
            print('KWin + Krohnkite, three clients:', initial, flush=True)
            for active, inactive in [(beta, alpha), (alpha, beta)]:
                output('xdotool', 'windowactivate', '--sync', active)
                expect_paper(active, normal_paper, 'active manuscript')
                expect_paper(inactive, INACTIVE_PAPER, 'neighboring manuscript')
            print('Rendered manuscript colors: active', normal_paper, 'inactive', INACTIVE_PAPER, flush=True)
            short = min((alpha, beta), key=lambda win: rectangle(win)[3])
            assert rectangle(short)[3] < screen[1] * .7, 'the layout must exercise a short editor tile'
            for chord in ('ctrl+f', 'Escape', 'ctrl+h', 'Escape'):
                key(short, chord)
                unchanged()
            key(beta, 'ctrl+Next')
            output('xdotool', 'type', '--clearmodifiers', '--delay', '20', ' Qzvexblim')
            second_text += ' Qzvexblim'
            key(beta, 'ctrl+s')
            key(beta, 'F8')
            review = wait_for('keyboard spelling review', lambda: next((window for title, window in windows().items()
                              if title.startswith('Spelling — American English')), None))
            key(review, 'alt+d')
            dictionary = Path(str(second) + '.dictionary')
            wait_for('added spelling saved in manuscript dictionary',
                     lambda: dictionary.exists() and 'qzvexblim' in dictionary.read_text().casefold().splitlines())
            if review in windows().values():
                key(review, 'Escape')
            key(beta, 'ctrl+Next')
            for chord, alias, first_name in [('alt+1', 'blue cap', 'Captain, Widgets'),
                                             ('alt+2', 'pebblepost', 'Planet Pebble')]:
                key(beta, chord)
                output('xdotool', 'type', '--clearmodifiers', '--delay', '24', alias)
                key(beta, 'ctrl+Return')
                key(beta, 'ctrl+s')
                second_text += first_name
                wait_for(f'first synonym inserted at the last crumb via {chord}',
                         lambda: second.read_text() == second_text + '\n')
                unchanged()
            print('Both synonym lists filtered and inserted their first names from the keyboard.', flush=True)
            key(beta, 'alt+w')
            clients.remove(beta)
            wait_for('closing the extra manuscript retiles the others', lambda: tiles(clients, screen))

            key(alpha, 'alt+w')
            peer_process.terminate()
            wait_for('closing test documents', lambda: alpha not in windows().values()
                     and peer not in windows().values())
            assert first_process.wait(timeout=8) == 0
            check_live_resize(app, run, launch, screen)
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


def check_live_resize(app, run, launch, screen):
    path = run / 'Resize.txt'
    first = 'This proportional text wraps across several visual lines. ' * 4
    manuscript = '\n'.join(f'Crumb {n}: Genre #{n}: {first}End.' for n in range(1, 3372))
    path.write_text(manuscript + '\n\n', encoding='utf-8')
    launch(app, path)
    writer = wait_for('large manuscript at EOF', lambda: windows().get('Resize.txt — Crumb Writer'))
    wide = wait_for('wide manuscript tile', lambda: tiles([writer], screen))[0]

    peer_proc = launch('xterm', '-title', 'Resize peer', '-e', 'sleep', '120')
    peer = wait_for('resizing editor with Krohnkite', lambda: windows().get('Resize peer'))
    narrow = wait_for('two tiles with large manuscript', lambda: tiles([writer, peer], screen))[0]
    assert narrow[2] < wide[2], (narrow, wide)
    output('xdotool', 'windowactivate', '--sync', writer)
    key(writer, 'ctrl+Next')
    text = 'Typing during a window resize.'
    with subprocess.Popen(['xdotool', 'type', '--clearmodifiers', '--delay', '45', text]) as typing:
        time.sleep(.15)
        peer_proc.terminate()
        wait_for('retiling to full window width', lambda: rectangle(writer)[2] == wide[2])
        assert typing.wait(timeout=8) == 0
    output('xdotool', 'key', '--clearmodifiers', 'ctrl+s')
    expected = (manuscript + text + '\n\n').encode()
    wait_for('typing saved at last crumb', lambda: path.read_bytes() == expected)
    print(f'Krohnkite resized a 3,371-crumb manuscript from {narrow[2]} to {wide[2]} pixels '
          'during typing. All input reached the final body.', flush=True)


def isolated(app, package, existing_display=False):
    with tempfile.TemporaryDirectory(prefix='crumbwriter-kwin-') as tmp:
        run = Path(tmp)
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', KWIN_COMPOSE='N', QT_QUICK_BACKEND='software')
        env.setdefault('LC_ALL', 'C.UTF-8')
        sampler = run / 'sample-paper'
        flags = shlex.split(output('pkg-config', '--cflags', '--libs', 'x11'))
        subprocess.run(['cc', '-std=c11', str(Path(__file__).with_name('sample-x11-paper.c')),
                        '-o', str(sampler), *flags], check=True)
        env['CW_TEST_PAPER_SAMPLER'] = str(sampler)
        for kind in ('config', 'data', 'state', 'cache', 'runtime'):
            subdir = run / kind
            subdir.mkdir(mode=0o700)
            env['XDG_' + kind.upper() + ('_DIR' if kind == 'runtime' else '_HOME')] = str(subdir)
        (run/'config'/'kwinrc').write_text(
            '[Plugins]\nkrohnkiteEnabled=true\n[Compositing]\nEnabled=false\n'
            '[Script-krohnkite]\nlogging=true\nlogNewWindows=true\nlogFilteredWindows=true\n'
            'logUnmanagedWindows=true\n')
        shutil.copytree(package, run/'data'/'kwin'/'scripts'/'krohnkite')
        command = ['dbus-run-session', '--']
        if not existing_display:
            command.extend(['xvfb-run', '-a', '-s', '-screen 0 1000x760x24'])
        subprocess.run([*command, sys.executable, __file__, '--session', app, run],
                       env=env, check=True, timeout=240)


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == '--session':
        session(Path(sys.argv[2]), Path(sys.argv[3]))
    elif len(sys.argv) == 4 and sys.argv[1] == '--existing-display':
        isolated(Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve(), existing_display=True)
    elif len(sys.argv) == 3:
        isolated(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
    else:
        sys.exit('usage: check-krohnkite.py [--existing-display] CRUMBWRITER UNPACKED_KROHNKITE_PACKAGE')

# Crumb Writer

A keyboard-driven C editor for numbered, single-line crumbs on KDE Plasma 6 and FreeBSD 15 or later. The manuscript stays plain UTF-8 text. A proportional font, word wrap, an italic header, and a bold title make each crumb readable on screen.

## Build on FreeBSD

Install the packages as root.

```sh
pkg install git pkgconf gtk3 gtksourceview4
```

Build and run as the desktop account. Launching without a filename opens a blank document. **Ctrl+O** opens an existing manuscript.

```sh
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
make
./crumbwriter
```

To open the manuscript directly, supply its actual path.

```sh
./crumbwriter /path/to/Crumbs.txt
```

From the Plasma session, `make check` runs the parser, storage, navigation, search, and typing tests. Optional system installation with `make install` as root adds the launcher to the applications menu. An installation under the desktop account is also available with `make install PREFIX="$HOME/.local"` when `$HOME/.local/bin` is on `PATH`.

## Writing

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Append the next crumb and start typing its body |
| Tab / Shift+Tab | Move to the next or previous header, title, or body |
| Ctrl+Up / Ctrl+Down | Move to the previous or next crumb |
| Up / Down | Move one visual line through wrapped text |
| Ctrl+F / Ctrl+H | Find or replace text, whole words, or regular expressions |
| F3 / Shift+F3 | Next or previous match, including with the search panel closed |
| Ctrl+G | Jump to a crumb number |
| F6 / F9 | Focus history or return to the editor / show or hide the sidebar |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+Shift+N / Ctrl+W | Open a blank document / close the current window |
| Ctrl+, / F10 | Settings / document menu |

The sidebar shows the current crumb’s number, title, official word count, recently visited crumbs, and new crumbs for the session. Click a history entry or select it with the arrow keys and press Enter. Search starts case-insensitive and remembers queries together with their options.

Smart punctuation turns typed quotes, apostrophes, double hyphens, and three periods into their typographic forms. The editor saves those characters to the manuscript. By default it follows the desktop theme, makes a timestamped sibling backup on open, and saves live at one-second intervals. Settings control appearance, font family, point size, smart punctuation, backup creation, and live saving.

[CrumbWriter.txt](CrumbWriter.txt) documents the file rules, save and recovery behavior, source map, and maintenance checks.

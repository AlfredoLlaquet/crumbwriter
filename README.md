# Crumb Writer

Crumb Writer 2.0 is a keyboard-driven C++ and Qt 6 manuscript editor for FreeBSD 15+ and KDE Plasma 6+. Each crumb occupies one physical line in a UTF-8 text file. On screen, the text wraps to the full width of the editor, with an italic header, a bold title, and a proportional body font. The sidebar shows the current crumb, its live word count, recent visits, numbered genres, and the number of new crumbs this session.

Programmed by **ChatGPT** for Alfredo Llaquet-Alsina.

## Build and run on FreeBSD

### Upgrade from 1.4

On a computer that already builds Crumb Writer 1.4, the new package is `qt6-base`. Install it as root.

~~~sh
pkg install qt6-base
~~~

Save your work and close all Crumb Writer windows. As your desktop account, run these commands in your existing `crumbwriter` checkout on `main`. FreeBSD’s standard `make` rebuilds it in place.

~~~sh
git pull --ff-only
make
./crumbwriter --version
./crumbwriter
~~~

The version check should print `Crumb Writer 2.0`. The last command starts that copy and reopens the last manuscript by default.

### First installation

Install the full package list as root.

~~~sh
pkg install git pkgconf glib qt6-base
~~~

Then build and launch as your desktop account.

~~~sh
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
make
./crumbwriter
~~~

**Ctrl+O** opens a manuscript. You can also pass its path to `./crumbwriter` when launching. Opening places the caret at the end of the text, just before a final newline when present, with the preceding writing visible. Run `make check` in a graphical session to exercise the parser, storage, and editor controls. [FreeBSD 15.1, Plasma/Krohnkite, and Linux test results for version 2.0](https://github.com/AlfredoLlaquet/crumbwriter/actions/runs/44833558438)

### Update the Plasma launcher

If you launch from Plasma’s application menu, repeat the installation from the rebuilt directory, using the same location as before. For a system-wide install under `/usr/local`, run `make install` as root. For a per-account install, run `make install PREFIX="$HOME/.local"` as your desktop account with `$HOME/.local/bin` on `PATH`. Both commands install the executable, launcher, icon, and manual.

## Writing and navigation

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Append the next crumb and start its body |
| Ctrl+1 | Select and copy the header, such as `Crumb 42`, leaving it selected |
| Ctrl+Q | Select and copy the entire crumb, excluding its ending newline and any first-line work title |
| Ctrl+Y | Delete the current crumb while the manuscript has focus |
| Escape | Clear the text selection and close Find or Replace when open |
| Tab / Shift+Tab | Next / previous header, title, or body |
| Ctrl+Up / Ctrl+Down | Previous / next crumb |
| Up / Down | Previous / next wrapped line, including virtual rows below the file |
| Home / End | Start / end of the wrapped line |
| Ctrl+Home / Ctrl+End | Start / end of the current crumb |
| Ctrl+Page Up / Ctrl+Page Down | Start / end of the entire file |
| Ctrl+Enter / Alt+Enter | Center the current visual line / place the current crumb at the top |
| Ctrl+L | Go to a crumb number |
| Ctrl+Shift+J | Follow a reference under the caret, such as `crumb 200` |
| F2 / Shift+F2 | Back / forward to the exact caret position after a jump |
| F4 / Shift+F4 | Next / previous numbering warning, with the expected and actual numbers |
| F6 / F7 | Focus visit history / genres from the manuscript. Type a fragment and press Enter. F6 returns focus to the manuscript |
| F9 | Show or hide the sidebar |
| Ctrl+B / Alt+B | Add a bookmark / open the separate Bookmarks window |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+Shift+N / Ctrl+W | Create an untitled window / close the current window when Find is closed |
| Ctrl+, / F10 / F1 | Settings / document menu / searchable keyboard reference |

A light blue background follows the current crumb across its wrapped lines. The writing area turns gray when the manuscript window loses focus. The editor follows the desktop appearance by default. Settings also offers Light, Dark, and a choice of font family and size.

Typing `Portrait #` in a title continues the most recent earlier Portrait. Activating Portrait in the genre list appends a complete crumb with the next numbered title. Bold red digits flag header numbers that disagree with their physical line and genre numbers that break their sequence.

Smart punctuation writes curly quotes and apostrophes, em dashes, and ellipses into the manuscript as typing supplies context. Paste cleans tabs, repeated spaces, and trailing spaces in one undoable operation. A space typed at the end of a crumb stays available for the next word. Saved files use UTF-8 without a BOM.

## Find and replace

**Ctrl+F** starts at the caret and searches forward. **Ctrl+G** starts there and searches backward. In plain-text mode, typing immediately selects and centers the nearest match. Extending the query searches from the original caret. **F3** repeats in the opening direction and **Shift+F3** reverses it for one match.

**Ctrl+H** opens Find and Replace. Press it again to switch focus between **Find** and **Replace with**. Replacement is available in plain-text and regular-expression modes. **All…** previews the affected crumbs with before-and-after examples. **Apply all** makes the changes, and one Undo reverses them.

| While Find or Replace is open | Action |
| --- | --- |
| Ctrl+P / Ctrl+R / Ctrl+N | Plain text / regular expression / crumb-number navigation |
| Ctrl+M / Ctrl+W / Alt+W | Toggle match case / whole words / wrap |
| Alt+Down | Open and focus recent searches |
| Enter / Shift+Enter in the query | Continue in the opening / opposite direction |
| Escape, including from the manuscript | Close search and clear the text selection |

Search starts case-insensitive, with wrap enabled. Regular expressions prefer the shortest match by default. For example, `“.*”` finds one quoted passage on a line, and `(?-U)“.*”` allows a match through the last closing quote. Recent queries include their search options. [PCRE2 syntax](https://www.pcre.org/current/doc/html/pcre2syntax.html) · [Pattern guide](https://www.pcre.org/current/doc/html/pcre2pattern.html)

## Bookmarks and saves

**Ctrl+B** bookmarks the current physical line. Choose a category and give the bookmark a name, or press Enter to use the next number in the last category. **Alt+B** opens the filterable tree or list with each bookmark’s line and current crumb title. A category with one bookmark opens that bookmark directly. A bookmark for line 200 always opens line 200.

A manuscript named `Crumbs.txt` keeps its bookmarks in `Crumbs.txt.bookmarks` in the same directory. **Save As** copies them beside the new manuscript.

Defaults include a timestamped backup beside the manuscript before editing and live saving in the background. Closing with pending changes offers **Save and Close**, **Discard Unsaved Changes**, and **Keep Editing**. Save and Close waits for the latest edit. A failed save keeps the window open.

[CrumbWriter.txt](CrumbWriter.txt) documents the file format, counting rules, keyboard controls, backup and recovery behavior, code structure, and maintenance checks.

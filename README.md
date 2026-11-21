# Crumb Writer

Crumb Writer 2.2 is a keyboard-driven C++ and Qt 6 manuscript editor for FreeBSD 15+ and KDE Plasma 6+. Each crumb occupies one physical line in a UTF-8 text file. On screen, text wraps to the full editor width with an italic header, a bold title, and a small gap between crumbs. The left panel holds Characters and Recurring. The right panel shows the current crumb and its live word count, recently visited titles, numbered genres, and new crumbs written today.

Programmed by **ChatGPT** for Alfredo Llaquet-Alsina.

## Build and run on FreeBSD

### Upgrade an existing checkout

A FreeBSD computer that already builds 2.1 has all the dependencies for 2.2. For an upgrade from 2.0, install the spelling engine and American English dictionary as root.

~~~sh
pkg install hunspell en-hunspell
~~~

For an upgrade directly from 1.4, include `qt6-base` in that command. Save your work and close all Crumb Writer windows. As your desktop account, run these commands in your existing `crumbwriter` checkout on `main`.

~~~sh
git pull --ff-only
make
./crumbwriter --version
./crumbwriter
~~~

The version check should print `Crumb Writer 2.2`. The last command starts that copy and reopens the last manuscript by default.

### First installation

Install the full package list as root.

~~~sh
pkg install git pkgconf glib qt6-base hunspell en-hunspell
~~~

Then build and launch as your desktop account.

~~~sh
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
make
./crumbwriter
~~~

**Ctrl+O** opens a manuscript. You can also pass its path to `./crumbwriter` when launching. Opening shows the end of the manuscript with the preceding writing visible. Run `make check` in a graphical session to exercise the parser, storage, spelling, and editor controls. [FreeBSD, Plasma/Krohnkite, and Linux checks](https://github.com/AlfredoLlaquet/crumbwriter/actions/workflows/check.yml)

### Update the Plasma launcher

If you launch from Plasma’s application menu, repeat the installation from the rebuilt directory, using the same location as before. For a system-wide install under `/usr/local`, run `make install` as root. For a per-account install, run `make install PREFIX="$HOME/.local"` as your desktop account with `$HOME/.local/bin` on `PATH`. Both commands install the executable, launcher, icon, and manual.

## Writing and navigation

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Append the next crumb and start its body |
| Ctrl+1 | Select the header and copy it in lowercase, such as `crumb 42` |
| Ctrl+Q | Select and copy the entire crumb, excluding its ending newline and any first-line work title |
| Ctrl+Y | Delete the current crumb while the manuscript has focus |
| Ctrl+W | Delete the selection, word, or space at the manuscript caret |
| Escape | Clear the text selection and close Find or Replace when open |
| Tab / Shift+Tab | Next / previous title or body |
| Ctrl+Up / Ctrl+Down | Previous / next crumb |
| Up / Down | Previous / next wrapped line, including virtual rows below the file |
| Home / End | Start / end of the wrapped line |
| Ctrl+Home / Ctrl+End | Start / end of the current crumb |
| Ctrl+Page Up / Ctrl+Page Down | Start of the file / end of the last crumb |
| Ctrl+Shift with Up, Down, Home, End, Page Up, or Page Down | Select to the corresponding crumb or file destination. Keep Shift held to extend or shrink from the same starting point |
| Ctrl+Space | Select by a vi movement typed in the manuscript |
| Ctrl+Enter / Alt+Enter | Center the current visual line / place the current crumb at the top |
| Ctrl+L | Go to a crumb number |
| Ctrl+P / Ctrl+R | Start plain-text / regular-expression Find from the caret |
| Ctrl+Shift+J | Follow a reference under the caret, such as `crumb 200` |
| F2 / Shift+F2 | Back / forward to the exact caret position after a jump |
| F4 / Shift+F4 | Next / previous numbering warning, with the expected and actual numbers |
| F6 / F7 | Focus visit history / genres from the manuscript. Type a fragment and press Enter. F6 returns focus to the manuscript |
| Shift+F7 | Switch genre order between alphabetical and lowest number first, with ties alphabetized |
| F9 / Shift+F9 | Show or hide the right / left panel |
| Alt+1 / Alt+2 | Focus Characters / Recurring |
| F8 / Shift+F8 | Next / previous spelling issue |
| Ctrl+Shift+D / Ctrl+Shift+I | Manuscript dictionary / live statistics |
| Ctrl+B / Alt+B | Add a bookmark / open the separate Bookmarks window |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo a run of typing, Backspaces, or forward Deletes |
| Ctrl+Shift+N / Alt+W | Create an untitled window / close the current window from the manuscript |
| Ctrl+, / F10 / F1 | Settings / document menu / searchable keyboard reference |
| F10, then A | About Crumb Writer. Enter opens the focused repository link |

A light blue background follows the current crumb across its wrapped lines. The writing area turns gray when the manuscript window loses focus. The editor follows the desktop appearance by default. Settings also offers Light, Dark, and a choice of font family and size.

Typing `Portrait #` in a title continues the most recent earlier Portrait. Activating Portrait in the genre list appends a complete crumb with the next numbered title. Bold red digits flag header numbers that disagree with their physical line and genre numbers that break their sequence.

Smart punctuation writes curly quotes and apostrophes, em dashes, and ellipses into the manuscript as typing supplies context. Paste cleans tabs, repeated spaces, and trailing spaces in one undoable operation. Backspacing over the last word leaves its preceding space at the caret for the replacement word, even through an autosave. Saved files omit trailing spaces and use UTF-8 without a BOM.

Consecutive typing forms one undo step, including spaces, pauses, automatic punctuation, and live saves. Consecutive Backspaces and consecutive forward Deletes each form their own step. Moving the caret, pressing Enter, pasting, or switching between typing and deletion starts another step.

Visit history displays each crumb’s title. Two crumbs with the same title have separate entries, and an empty title appears as *Untitled*. Each entry returns to the last caret position in that crumb.

### Select by a vi movement

Press **Ctrl+Space** in the manuscript, then type a command. The bottom bar shows what has been typed. For example, type `2t-` to select from the caret up to the second hyphen, even several crumbs away. The second hyphen stays outside the selection. The movement takes effect as soon as its last character is typed.

| After Ctrl+Space | Selection destination |
| --- | --- |
| `j` / `k` | Next / previous crumb, at the same character column when it fits |
| `gj` / `gk` | Next / previous wrapped row, at the same horizontal position when it fits |
| `h` / `l` | One character left / right |
| `w` / `b` / `e` | Start of the next word / start of a word going backward / through the end of a word |
| `W` / `B` / `E` | Same word movements, treating each run of non-whitespace as a word |
| `0` / `^` / `$` | Beginning / first nonblank / end of the physical line |
| `gg` / `G` / `42G` | First line / last line / physical line 42 |
| `f-` / `t-` | Through the next hyphen / up to it. `F` and `T` search backward |
| `;` / `,` | Repeat the last character find in its original / opposite direction |
| `q` followed by a phrase, then Enter | Forward through the whole phrase, ignoring case. `Q` searches backward |

Backspace corrects a pending command. Escape cancels it. A missing match leaves the original selection in place so the command can be corrected.

## Find and replace

**Ctrl+F** starts at the caret and searches forward. **Ctrl+G** starts there and searches backward. In plain-text mode, typing immediately selects and centers the nearest match. Extending the query searches from the original caret. **F3** repeats in the opening direction and **Shift+F3** reverses it for one match.

**Ctrl+H** opens Find and Replace. Press it again to switch focus between **Find** and **Replace with**. Replacement is available in plain-text and regular-expression modes. **All…** previews the affected crumbs with before-and-after examples. **Apply all** makes the changes, and one Undo reverses them.

| Find and Replace controls | Action |
| --- | --- |
| Ctrl+P / Ctrl+R / Ctrl+L | Plain text / regular expression / crumb-number navigation |
| Ctrl+M / Ctrl+W / Alt+W | Toggle match case / whole words / wrap |
| Alt+Down | Open and focus recent searches |
| Enter / Shift+Enter in the query | Continue in the opening / opposite direction |
| Escape, including from the manuscript | Close search and clear the text selection |

Search starts case-insensitive, with wrap enabled. Regular expressions prefer the shortest match by default. For example, `“.*”` finds one quoted passage on a line, and `(?-U)“.*”` allows a match through the last closing quote. Recent queries include their search options. [PCRE2 syntax](https://www.pcre.org/current/doc/html/pcre2syntax.html) · [Pattern guide](https://www.pcre.org/current/doc/html/pcre2pattern.html)

## Characters, Recurring, and spelling

Use **Alt+1** for Characters or **Alt+2** for Recurring. Type any alias to filter. **Insert** opens Add with the selected manuscript text suggested as one synonym. The suggestion is selected for editing or replacement. With an empty selection, the field opens blank. **Enter** saves the group. **F2** edits a group, and **Delete** removes it after confirmation. In the list, **Enter** finds every synonym and **Ctrl+Enter** inserts the first synonym at the manuscript caret.

Each group occupies one line in `Crumbs.txt.characters` or `Crumbs.txt.recurring`. The first alias determines the alphabetical order. For example, `"Fox, Captain", Red Fox, Captain Fox` has three aliases. Double quotes protect a comma inside an alias.

American English spelling is enabled by default. **F8** opens a correction with suggestions, Replace, Replace All, Ignore Once, Ignore All, and Add to Dictionary. **Ctrl+Shift+D** opens the manuscript’s dictionary. Each entry in `Crumbs.txt.dictionary` is one word, matched case-insensitively. Complete aliases in either list are accepted. A shorter name can be added as its own alias. Straight and curly apostrophes work in contractions. **Ctrl+,** opens Settings to switch spelling on or off.

## Statistics and daily count

**Ctrl+Shift+I** shows the number of recognized crumbs, the official whole-file word total including the first-line work title, and the average words per crumb to two decimal places. The figures share a right-aligned column and use the desktop locale’s number separators. **Ctrl+C** copies all three figures. **New today** remembers each manuscript’s count across sessions and resets on a new local date. Adding three crumbs and deleting one leaves two. Undo restores the third.

## Bookmarks and saves

**Ctrl+B** bookmarks the current physical line. Choose a category and give the bookmark a name, or press Enter to use the next number in the last category. **Alt+B** opens the filterable tree or list with each bookmark’s line and current crumb title. A category with one bookmark opens that bookmark directly. A bookmark for line 200 always opens line 200.

A manuscript named `Crumbs.txt` keeps its bookmarks in `Crumbs.txt.bookmarks` in the same directory. **Save As** carries the bookmarks, synonym lists, and personal dictionary beside the new manuscript.

Defaults include a timestamped backup beside the manuscript before editing and live saving in the background. Closing with pending changes offers **Save and Close**, **Discard Unsaved Changes**, and **Keep Editing**. Save and Close waits for the latest edit. A failed save keeps the window open.

[CrumbWriter.txt](CrumbWriter.txt) documents the file format, counting rules, keyboard controls, backup and recovery behavior, code structure, and maintenance checks.

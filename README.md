# Crumb Writer

A keyboard-driven C editor for numbered, single-line crumbs on KDE Plasma 6 and FreeBSD 15 or later. The manuscript stays plain UTF-8 text. A proportional font, word wrap, an italic header, and a bold title make each crumb readable on screen.

Programmed by **ChatGPT** for Alfredo Llaquet-Alsina.

## Build on FreeBSD

Install the packages as root.

```sh
pkg install git pkgconf gtk3 gtksourceview4
```

Build and run as the desktop account. On first launch, **Ctrl+O** opens an existing manuscript. Later launches without a filename reopen the last manuscript. Every open places the cursor at the end of the final line of content, ready to continue writing.

```sh
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
make
./crumbwriter
```

To open a particular manuscript directly, supply its actual path.

```sh
./crumbwriter /path/to/Crumbs.txt
```

From the Plasma session, `make check` runs the parser, storage, navigation, search, and typing tests. Optional system installation with `make install` as root adds the launcher to the applications menu. An installation under the desktop account is also available with `make install PREFIX="$HOME/.local"` when `$HOME/.local/bin` is on `PATH`.

For an existing checkout, close the running application, run `git pull --ff-only`, then `make`. Run `./crumbwriter --version` to identify the build and `./crumbwriter` to launch it. Repeat the original `make install` command to update an installed Plasma launcher.

## Writing

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Append the next crumb and start typing its body |
| Ctrl+Q | Select the current crumb, copy it to the system clipboard, and leave it selected |
| Ctrl+Y | Delete the crumb at the caret, including its line separator where needed |
| Esc | Clear the text selection and close Find or Replace if visible |
| Tab / Shift+Tab | Move to the next or previous header, title, or body |
| Ctrl+Up / Ctrl+Down | Move to the previous or next crumb |
| Up / Down | Move one visual line through wrapped text |
| Ctrl+Home (Begin) / Ctrl+End | Beginning / end of the current crumb |
| Ctrl+Page Down (Next) | Beginning of the file, including any work title |
| Ctrl+Page Up (Prior) | End of the file, including any final newline |
| Ctrl+F / Ctrl+G | Find forward / backward from the manuscript caret |
| Ctrl+H | Open Find and Replace |
| F3 / Shift+F3 | Next or previous match, including with the search panel closed |
| Ctrl+F, then Ctrl+N | Jump to a crumb number |
| F6 / F9 | Focus history or return to the editor / show or hide the sidebar |
| F7, then arrows and Enter | Pick a genre and append its next numbered crumb |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+Shift+N / Ctrl+W | Open a blank document / close the current window |
| Ctrl+, / F10 | Settings / document menu |
| F1 | Open searchable keyboard shortcuts |

The sidebar shows the current crumb’s number, title, official word count, recently visited crumbs, and new crumbs for the session. Click a history entry or select it with the arrow keys and press Enter. Bold red digits flag a crumb number that differs from its physical line number or a genre number that breaks its sequence. Each genre starts at #1 and continues from its previous occurrence. The flags are display formatting. Editing, copying and saving preserve the manuscript’s actual numbers.

Ctrl+Q excludes the ending newline and the optional first-line work title. Ctrl+Y preserves that work title and the file’s final-newline convention. Ctrl+Z restores the deleted crumb in a single step. Navigation and scrolling move the document immediately.

**F1** opens the shortcut reference alongside the manuscript. It includes the commands for editing, the sidebar, Find and Replace, and the document window. The same reference appears under **Document menu → Keyboard Shortcuts**.

When writing a numbered genre title, type the genre name followed by a space and `#`. If the last earlier `Portrait` was `Portrait #42`, typing `Portrait #` completes `Portrait #43`. The cursor stays after the number.

The sidebar also lists genres alphabetically with the last number used for each. Clicking **Portrait · #42**, for example, appends the next crumb with the title **Portrait #43** and starts the cursor in its body. One Undo removes the entire new crumb.

Smart punctuation turns typed quotes, apostrophes, double hyphens, and three periods into their typographic forms. The editor saves those characters to the manuscript. By default it follows the desktop theme, makes a timestamped sibling backup on open, and saves live at one-second intervals. Settings control appearance, font family, point size, smart punctuation, backup creation, and live saving.

## Finding a passage

**Ctrl+F** finds the next occurrence from the caret. **Ctrl+G** finds the previous one. With plain text, each keystroke selects the nearest match in that direction and keeps keyboard focus in the query. Extending the query starts again at the original caret. Enter continues in the chosen direction. F3 always goes forward and Shift+F3 always goes backward. Search is case-insensitive by default, wraps at the document ends, and remembers completed queries with their options.

| While Find or Replace is open | Action |
| --- | --- |
| Ctrl+P / Ctrl+R / Ctrl+N | Plain text / regular expression / crumb number |
| Alt+Down | Open and focus recent searches |
| Ctrl+M / Ctrl+W / Alt+W | Toggle match case / whole words / wrap |
| Esc, including from the manuscript | Close search and clear the selection |

Regular expression search prefers the shortest match by default. `Haiku #[0-9]+:` finds a numbered Haiku title through its colon. `“.*”` finds one quoted passage at a time on a line. To match from the first opening quote through the last closing quote on that line, use `(?-U)“.*”`. Parentheses capture text for use in replacements such as `\1`. [PCRE2 syntax reference](https://www.pcre.org/current/doc/html/pcre2syntax.html) · [PCRE2 pattern guide](https://www.pcre.org/current/doc/html/pcre2pattern.html)

[CrumbWriter.txt](CrumbWriter.txt) documents the file rules, keyboard controls, design decisions, source structure, and checks. Every change to Crumb Writer includes an update to that manual.

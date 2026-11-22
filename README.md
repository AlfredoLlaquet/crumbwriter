# Crumb Writer

Crumb Writer 2.5.1 is a keyboard-driven Qt 6 editor for FreeBSD with KDE Plasma, Ubuntu, Windows, and macOS. Use **Crumb mode** for a numbered collection of short pieces or **Generic mode** for a novel, an essay, or other paragraph-based prose. Both write plain UTF-8 without a byte order mark (BOM) and wrap the text to the full width of the writing area.

Programmed by **ChatGPT** for Alfredo Llaquet-Alsina.

## Download Crumb Writer

Crumb Writer is free to download and use. The installer links below open the latest published release. Its release page gives the version. The build instructions use the current source.

| Platform | Download or build |
| --- | --- |
| Windows 10 (1903 or later) or Windows 11, x64 | [Download the Windows installer (.exe)](https://github.com/AlfredoLlaquet/crumbwriter/releases/latest/download/CrumbWriter-Windows-x64-Setup.exe) |
| macOS 15 or later, Apple silicon or Intel | [Download the Mac disk image (.dmg)](https://github.com/AlfredoLlaquet/crumbwriter/releases/latest/download/CrumbWriter-macOS-Universal.dmg) |
| FreeBSD 15 or later with KDE Plasma 6 | [Build and install on FreeBSD](#build-and-run-on-freebsd) |
| Ubuntu | [Build and install on Ubuntu](#build-and-run-on-ubuntu) |

The Windows and Mac downloads include Qt and American English spelling. The same manuscript and its bookmark, character, recurring-term, and dictionary files can travel between all four platforms. Each release page identifies the version and includes its checksums and corresponding source. [All releases](https://github.com/AlfredoLlaquet/crumbwriter/releases)

### License and warranty

Crumb Writer’s original code is dedicated to the public domain. Anyone may use, copy, modify, sell, or redistribute it. The software is provided “as is,” without warranty. To the fullest extent permitted by applicable law, the authors disclaim liability for claims or damages arising from its use. This dedication applies only to the editor. Writers retain their rights to their manuscripts. [Full text of the Unlicense](LICENSE)

Bundled libraries and the spelling dictionary have their own terms. The Windows installation folder and the Mac app’s `Contents/Resources` folder contain `third-party/README.txt` and the individual notices. Corresponding library sources and build recipes are available alongside the installers on each release page. Inside the editor, **About Crumb Writer → View License** opens the full Crumb Writer license offline.

### Install or update on Windows

1. Download the Windows installer. For confirmation that the file came from this repository, [verify the download](#verify-a-download).
2. Open the installer. It installs for the current account and provides a Start menu entry. Setup also offers a desktop shortcut, a **View License** button, and the option to list Crumb Writer under **Open with** for text files while preserving the current default editor. Open **Crumb Writer** from Start.
3. To update, close the editor and run the newer installer. To uninstall, select **Crumb Writer** under **Settings → Apps → Installed apps** (called **Apps & features** on Windows 10). Manuscripts, their companion files, and personal settings are kept.

The installer has no Windows publisher certificate. SmartScreen may display **Windows protected your PC** or an unknown publisher. After verifying the file, **More info → Run anyway** allows this installation where Windows policy offers that choice. [Microsoft’s guide to SmartScreen](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

### Install or update on macOS

1. Download the Mac disk image. The same file runs natively on Apple silicon and Intel Macs. [Verify the download](#verify-a-download).
2. Open the disk image in Finder and drag **Crumb Writer** to **Applications**. With the keyboard, select the app in the disk image, press **Command+C**, open Applications with **Command+Shift+A**, and press **Command+V**. Eject the disk image and open the installed copy.
3. The app carries an ad hoc signature and has no Apple notarization. On the first launch, Gatekeeper may ask for approval. After attempting to open the verified app, open **System Settings → Privacy & Security**, locate the notice for **Crumb Writer**, and choose **Open Anyway**. This grants an exception to this copy of the app. [Apple’s guide to opening downloaded apps](https://support.apple.com/en-us/102445)
4. To update, quit Crumb Writer and replace the application with the newer copy. To uninstall, move the application to Trash. Manuscripts and their companion files stay wherever they were saved.

### Verify a download

GitHub Actions creates a signed build record, called an **artifact attestation**, for the exact installer and disk image. With GitHub CLI installed and signed in, run the command for the file just downloaded, in the folder containing it.

~~~text
gh attestation verify CrumbWriter-Windows-x64-Setup.exe --repo AlfredoLlaquet/crumbwriter
gh attestation verify CrumbWriter-macOS-Universal.dmg --repo AlfredoLlaquet/crumbwriter
~~~

A successful check identifies the repository, build workflow, and source revision that produced the downloaded bytes. Windows SmartScreen and Apple Gatekeeper make their own decisions when opening the program. [GitHub CLI verification guide](https://cli.github.com/manual/gh_attestation_verify)

For a quick integrity check, compare the file’s SHA-256 digest with its entry in `SHA256SUMS` on that release page. In Windows PowerShell, run `Get-FileHash .\CrumbWriter-Windows-x64-Setup.exe -Algorithm SHA256`. In Mac Terminal, run `shasum -a 256 CrumbWriter-macOS-Universal.dmg`.

The repository uses GitHub’s **immutable releases**. When a release is published with this setting, GitHub locks the tag to its source commit and locks the attached files. The signed build record independently lets a reader verify the downloaded bytes. [GitHub’s immutable release guide](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)

## Build and run on FreeBSD

### First installation

On FreeBSD 15 or later with Plasma 6, install the dependencies as root.

~~~sh
pkg install git pkgconf glib qt6-base hunspell en-hunspell
~~~

Get the source as your desktop account.

~~~sh
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
~~~

From that checkout, run the following command as root. It builds the application and installs the executable, desktop launcher, icons, manual, and license under `/usr/local`.

~~~sh
make install
~~~

Open the Plasma application launcher, type **Crumb Writer**, and press **Enter**. **Ctrl+O** opens a manuscript and places the caret at the end. The next launch normally reopens the last manuscript. **F1** opens the searchable keyboard reference.

For a personal installation, run `make install PREFIX="$HOME/.local"` as your desktop account. That account’s Plasma session needs `$HOME/.local/bin` on its `PATH` so the launcher can find the executable.

### Update

An installation that already builds versions 2.1 through 2.5 has the dependencies for 2.5.1. For older checkouts, run the package command under First installation. Save the manuscript, close all Crumb Writer windows, and update the existing checkout as your desktop account.

~~~sh
cd crumbwriter
git pull --ff-only
~~~

In the updated directory, repeat the original installation command and prefix. For a system installation, run `make install` as root. Reopen **Crumb Writer** from Plasma. **F10**, then **A**, opens About, which shows the installed version.

## Build and run on Ubuntu

The Linux test job uses **Ubuntu 24.04**. From a terminal on the desktop account, install the build tools, Qt 6, GLib, Hunspell, and the American English dictionary, then build and install the desktop application.

~~~sh
sudo apt update
sudo apt install git build-essential pkgconf qt6-base-dev qt6-wayland libglib2.0-dev libhunspell-dev hunspell-en-us libxcb-cursor0
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
sudo make install
~~~

Open the desktop application menu, search for **Crumb Writer**, and press **Enter**. For subsequent updates, close the application, run `git pull --ff-only` in the checkout, and run `sudo make install` again. On either platform, `make` as your desktop account can perform the compilation ahead of the system installation. Run `make check` in a graphical session for the parser, storage, and editor tests. [FreeBSD, Ubuntu, and Plasma/Krohnkite checks](https://github.com/AlfredoLlaquet/crumbwriter/actions/workflows/check.yml)

## Writing Crumbs in Crumb Mode

A crumb is a short piece with three fields on one physical file line. The first colon ends the **header**, and the second ends the **title**. The rest of the line is the **body**, which can contain additional colons. A header consists of the manuscript’s **label**, a space, and the **number** of the piece. Start at 1 so that crumb 200 occupies line 200. Title and body may both be empty.

The first file line does double duty. It begins with the title of the whole work, immediately followed by `//` and the first crumb. Give each manuscript a distinctive label. In the following example the work is *Cranolos - Manolo’s Crumbs*, its label is `Cranolo`, and the first piece is *The Inspector*.

~~~text
Cranolos - Manolo’s Crumbs//Cranolo 1: The Inspector: A goose audits the moon for bumps.
Cranolo 2: Night Audit #1: The tide submits its receipts.
Cranolo 3: Night Audit #2: One receipt smells of salt.
~~~

Here the third piece has header number **3** and genre number **2**. The header number locates a piece in the manuscript. A sequence such as *Night Audit* can recur anywhere in the collection and has its own numbering. Type `Night Audit #` in a later title to continue from the nearest earlier occurrence. For a new entry at the end, press **Ctrl+Shift+4**, type part of the genre, and press **Enter**. The editor supplies the next header and title and leaves the caret ready for the body. **Ctrl+N** appends a crumb with an empty title. Bold red digits flag a mismatch with the file line or a break in a genre’s sequence. **F4** visits the next warning.

On screen the header is italic, the title is bold, and the body uses the chosen writing font. A long piece may cross many screen rows while keeping its one file line. **Tab** moves between titles and bodies, **Ctrl+Down** moves to the next crumb, and **Ctrl+Q** selects and copies the current piece. The work title stays outside that selection. A visit appears under the piece’s own title, so two pieces with the same title can still be revisited independently. To start a collection in a blank editor, type the work title followed immediately by `//`, then press **Ctrl+N** to create the first header on that line. Change its default label, `Crumb`, to the collection’s own label once. The next header will reuse it.

A publisher can use those same field boundaries to present a piece on the web. The official Crumb Reader fetches Alfredo Llaquet-Alsina’s published `Crumbs.txt`, checks each `Crumb` number against its line, and separates its title and body. In the manuscript, the first title after `Crumb 1:` is *The Game*. Its direct link opens that piece in the Reader, where the reader can move on, search the collection, or mark a favorite. The source line, parser, and reading view show the passage from manuscript to publication. [Published manuscript, line 1](https://github.com/AlfredoLlaquet/Alfredo_Llaquet-Alsina_Crumbs/blob/main/Crumbs.txt#L1) · [Reader parser](https://github.com/AlfredoLlaquet/Alfredo_Llaquet-Alsina_Crumbs/blob/main/Reader/core.js) · [Read *The Game*](https://www.thecrumbs.org/Reader/?crumb=1)

## Writing Novels and Other Prose in Generic Mode

Generic mode gives a novel, an essay, or a collection of prose a working manuscript that can also serve as its long-term source. Each press of **Enter** ends a paragraph. Let the editor wrap long paragraphs as they reach the edge of the window. Narrowing a tiled window may double the number of rows a sentence occupies on screen, but its paragraph still has the same place in the file. Consecutive paragraphs go on consecutive lines. Reserve blank lines for structure, especially around chapter headings.

Consider a small novel manuscript named `Harbor.txt`. These are its first six file lines, including the two empty lines.

~~~text
The Harbor of Borrowed Tides

The Night Clerk

At dusk the harbor clerk signed for a tide that had arrived in six wooden crates.
By dawn the water had learned the shape of the pier.
~~~

The general title appears in the largest bold type. The editor recognizes a **nonblank first line** as the general title if **any one** of three conditions holds. It contains no period (`.`), its letters are all uppercase, or the next line is blank. A later nonblank paragraph with a blank line immediately before **and** after it becomes a chapter title. That makes *The Night Clerk* a chapter heading. It appears in slightly smaller bold type. The blank lines remain in the file and occupy very little height on screen. The two sentences beneath the heading are distinct prose paragraphs with ordinary spacing. A chapter heading can contain punctuation, and a single sentence surrounded by blank lines receives exactly the same treatment. At the very end of a file, a single Enter finishes the last line. A second Enter supplies a blank line after it.

While drafting, **Ctrl+Up** and **Ctrl+Down** travel by paragraph. **Tab** and **Shift+Tab** travel between headings. **Ctrl+Shift+4** focuses **Titles**, which starts in manuscript order. Type part of a title and press **Enter** to jump there. **Shift+F7** switches between manuscript and alphabetical order. Visit history labels paragraphs by their opening words and searches their full text. After moving to another chapter, press **Ctrl+Shift+3**, type `six wooden crates`, and press **Enter** to return to the visited paragraph, even when the phrase falls beyond its displayed opening. The right panel shows **New words written today**. A day spent cutting 150 more words than were added shows −150. Closing and reopening the same manuscript that day retains its count.

Those same six lines are usable outside the writing desktop. For example, this command finds a phrase and prints its physical line number with the entire matching paragraph.

~~~sh
grep -nF 'six wooden crates' Harbor.txt
~~~

The result begins `5:At dusk`. A revision comparison can likewise show a change to that paragraph as one changed line. Search, backup, and version-control tools can all read the original directly. Accented names, curly quotes, and em dashes travel with the text as UTF-8 characters. The outline can be reconstructed from the first line and the blank neighbors of each heading. Fonts, window dimensions, and page breaks belong to the reading or publication layout.

A publishing script can turn the first line into an HTML `<h1>`, *The Night Clerk* into an `<h2>`, and each of the next two paragraphs into a separate `<p>`. The same source can feed a typeset PDF or a reflowable ebook with its own margins and type sizes. A correction to the crates sentence goes into `Harbor.txt` once and reaches each edition the next time it is generated. Years later, the six lines can still be read in an ordinary text editor, even without the publishing scripts or the original desktop.

**Mode follows the manuscript.** A `//` delimiter outside a URL on the first physical line activates Crumb mode. A first line without that delimiter, including an empty line or an ordinary web address such as `https://www.thecrumbs.org/`, activates Generic mode. An older collection that starts directly with `Crumb 1:` can activate Crumb mode by adding the work title and `//` before its first header. Editing that line switches the mode immediately. The right end of the status bar shows the current mode.

## How Words Are Counted

A crumb’s count includes everything from the header label through the end of its body. Count each run between spaces or other word-separating whitespace. An em dash also separates words. A slash with an ordinary space immediately on each side acts as a separator. Hyphens, apostrophes, and punctuation attached to a word stay with it. For example, `moon-lit / sand—bank` counts as three words. A separate punctuation token counts too, so `Crumb 2: :` has three words.

The current-crumb panel skips the general work title on line 1. The **whole-manuscript total includes it**. In the Cranolos example the first crumb has 11 words. Its full file line has 15 because `Cranolos - Manolo’s Crumbs` contributes four, including the separately spaced hyphen. The average divides the whole-file total by the number of recognized crumbs.

The official reference on FreeBSD and Linux is the following command. It also runs on macOS. On line 1, the first substitution replaces the first literal `//` with a single space. On every line, the remaining substitutions replace each em dash and each nonoverlapping literal ` / ` with a space. Finally, `wc -w` counts the resulting words. For the first **individual** crumb, remove the work title through the actual delimiter and apply the last two substitutions to the rest of that line.

~~~sh
sed -e '1s@//@ @' -e 's/—/ /g' -e 's@ / @ @g' Crumbs.txt | wc -w
~~~

The exact count for unusual Unicode characters depends on the platform’s `wc -w`. FreeBSD and macOS check the remaining UTF-8 bytes individually. On Ubuntu, GNU `wc` reads whole characters in a UTF-8 locale. The Windows build reproduces FreeBSD 15’s `C.UTF-8` result, independent of the Windows display language. [FreeBSD implementation](https://github.com/freebsd/freebsd-src/blob/releng/15.0/usr.bin/wc/wc.c) · [Apple implementation](https://github.com/apple-oss-distributions/text_cmds/blob/main/wc/wc.c) · [GNU word counting](https://www.gnu.org/software/coreutils/manual/html_node/wc-invocation.html)

Generic prose uses the same whitespace, em dash, and spaced slash rules. A URL keeps its slashes. Manuscript Statistics includes headings among the nonblank paragraphs and excludes blank separators from the paragraph count and average.

## Writing and navigation

On a Mac, **Ctrl** means the physical **Control** key and **Alt** means **Option**. The usual **Command** shortcuts also work for New Window, Open, Save, Save As, Find, Select All, Cut, Copy, Paste, Undo, and Redo. **Command+,** opens Settings, **Command+W** closes the current window, and **Command+Q** quits the editor through its normal save confirmation. **Control+Shift+Space** starts movement selection when **Control+Space** switches input sources. **Control+Shift+B** opens Bookmarks, and **Control+Shift+W** toggles Wrap in Find, leaving Option available for typing special characters. On keyboards whose top row controls media, hold **Fn** for function keys such as **F1**.

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Append a crumb and start its body, or start a new paragraph at the end in Generic mode |
| Ctrl+1 | In Crumb mode, select the header and copy it in lowercase, such as `crumb 42` |
| Ctrl+Q | Select and copy the current crumb or paragraph, excluding its ending newline and any first-line work title in Crumb mode |
| Ctrl+Y | Delete the current crumb or paragraph while the manuscript has focus |
| Ctrl+W | Delete the selection, word, or space at the manuscript caret |
| Escape | Clear the text selection and close Find or Replace when open |
| Tab / Shift+Tab | Move between crumb titles and bodies, or between headings in Generic mode |
| Ctrl+Up / Ctrl+Down | Previous / next crumb or paragraph |
| Up / Down | Previous / next wrapped line, including virtual rows below the file |
| Home / End | Start / end of the wrapped line |
| Ctrl+Home / Ctrl+End | Start / end of the current crumb or paragraph |
| Ctrl+Page Up / Ctrl+Page Down | Start of file / end of last crumb or paragraph |
| Ctrl+Shift with Up, Down, Home, End, Page Up, or Page Down | Select to the corresponding line or file destination. Keep Shift held to extend or shrink from the same starting point |
| Ctrl+Space | Preview a movement selection from the caret. Enter accepts it |
| Ctrl+Enter / Alt+Enter | Center the current visual line / place the current crumb or paragraph at the top |
| Ctrl+L | Go to a header number in Crumb mode or a physical line number in Generic mode |
| Ctrl+P / Ctrl+R | Start plain-text / regular-expression Find from the caret |
| Ctrl+U | Open Insert Symbol with the search field focused |
| F5 in the manuscript | Accept a suggested name ending or open a list of completions |
| F6 / Shift+F6 | Previous / next mention of the same character or recurring term |
| Ctrl+Shift+J / Ctrl+click | Follow a numbered crumb reference or find the previous mention of a synonym |
| Ctrl+Shift+click | Find the next mention of the clicked name |
| F2 or Alt+Left | Go back to the exact writing position before a jump |
| Shift+F2 or Alt+Right | Go forward in the same history |
| F4 / Shift+F4 | Next / previous numbering warning, with the expected and actual numbers |
| Ctrl+Shift+3 / Ctrl+Shift+4 | Search Visit history / Genres or Titles |
| Shift+F7 | Change genre order (name or last number) or title order (document or alphabetical) |
| F9 / Shift+F9 | Show or hide navigation / synonym lists |
| Ctrl+Shift+F10 | Arrange panels and columns in the current window |
| Ctrl+Shift+1 / Ctrl+Shift+2 | Search Characters / Recurring |
| F8 / Shift+F8 | Next / previous spelling issue |
| Ctrl+Shift+D / Ctrl+Shift+I | Manuscript dictionary / live statistics |
| Ctrl+B / Alt+B | Add a bookmark / open the separate Bookmarks window |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo a run of typing, Backspaces, or forward Deletes |
| Ctrl+Shift+N / Alt+W | Create an untitled window / close the current window from the manuscript |
| Ctrl+, / F10 / F1 | Settings / document menu / searchable keyboard reference |
| F10, then A | About Crumb Writer. Enter opens the repository link. Alt+L opens the license offline |

Press **Ctrl+,** and choose **Appearance** to follow the desktop or select **Light** or **Dark**. The choice applies to the toolbar, search fields, lists, menus, and editor dialogs. The three-line button beside **Find** opens the document menu (**F10**). The same symbol beside **Characters** and **Recurring** opens actions for that list. A focused menu button opens with **Space**, **Enter**, or **Down**. **Escape** closes the menu.

A light blue band follows the current crumb or paragraph in a light theme. Dark themes use a slightly lighter neutral gray across its wrapped lines. When another window becomes active, the whole writing area changes to a softer gray. Search and the side panels belong to the active manuscript window. The manuscript font and size can also be changed in Settings.

Smart punctuation writes curly quotes and apostrophes, em dashes, and ellipses into the manuscript as typing supplies context. Paste cleans tabs, repeated spaces, and trailing spaces in one undoable operation. Backspacing over the last word leaves its preceding space at the caret for the replacement word, even through an autosave. Saved files omit trailing spaces and use UTF-8 without a BOM.

Consecutive typing forms one undo step, including spaces, pauses, automatic punctuation, and live saves. Consecutive Backspaces and consecutive forward Deletes each form their own step. Moving the caret, pressing Enter, pasting, or switching between typing and deletion starts another step.

**Copy deleted text to the clipboard** is enabled in **Settings → Writing**. Backspacing through `goose` or deleting it forward leaves `goose` ready to paste. Deleting a word, selection, or whole crumb copies the removed text. Pasting over a selection first inserts the incoming clipboard contents, then puts the displaced passage on the clipboard. Replace All collects the removed passages in manuscript order, separated by line breaks. Automatic punctuation, whitespace cleanup, and Undo/Redo preserve that clipboard entry.

Undo immediately after **Ctrl+N** returns the caret, selection, and view to their positions before the new crumb or paragraph was appended. Redo takes the caret back to the new writing position.

Visit history displays crumb titles or the beginnings of generic paragraphs. An empty crumb title appears as *Untitled*. Each entry returns to its last caret position.

### Insert a Unicode symbol

Press **Ctrl+U** to open **Insert Symbol**. Type `outline heart`, `music`, a literal symbol, or a code point such as `U+2661`. The grid searches 733 symbols in 17 themes. The preview shows the selected glyph in the manuscript font, its name, and its code point. Themes narrow the selection to related symbols, including typography, mathematics, arrows, and music.

**Alt+S** or **Ctrl+F** focuses Search. **Alt+T** or **Ctrl+T** opens the theme selector. **Down** moves from Search into the grid, where the arrow keys browse. **Enter** or a double-click inserts and closes. **Ctrl+Enter** inserts and keeps the picker open. **Ctrl+D** toggles a favorite, and **Escape** closes. Favorites and recently used symbols are available across manuscripts on the same computer. A chosen symbol is inserted as ordinary Unicode text in one undo step. [Catalog and Unicode data notice](data/symbols.tsv)

### Arrange panels and columns

Use **View → Arrange Panels and Columns** or **Ctrl+Shift+F10** in a configurable window. Choose a panel or column, press **Space** to show or hide it, and use the move and size controls to adjust its position or dimensions. The editor remembers the arrangement for each kind of window on that computer. A floating window can recover its size. When Krohnkite tiles two manuscripts, each takes the space assigned by the window manager.

**View → Restore Default Layouts** restores the original arrangements across windows. Manuscript text, bookmarks, dictionaries, and writing preferences keep their own settings.

### Live movement selections

Press **Ctrl+Space** in the manuscript. Type a movement, add an optional count **after** it, and press **Enter** to keep the selection. The bottom bar shows the command. Every keystroke previews the result from the same starting caret. For example, `j` selects into the next file line, `j2` selects into the second, and Backspace returns to one. **Escape** restores the opening caret and selection. An incomplete command or a missing match can be corrected before pressing Enter.

| After Ctrl+Space | Selection destination |
| --- | --- |
| `j` / `k` | Next / previous physical line, at the same character column when it fits |
| `gj` / `gk` | Next / previous wrapped row, at the same horizontal position when it fits |
| `h` / `l` | One character left / right |
| `w` / `b` / `e` | Start of next word / start of a word backward / through the end of a word |
| `W` / `B` / `E` | Same word movements, treating each run of non-whitespace as a word |
| `0` / `^` / `$` | Beginning / first nonblank / end of the physical line |
| `gg` / `G` / `G42` | First line / last line / physical line 42 |
| `f-` / `t-2` | Through the next hyphen / up to the second hyphen. `F` and `T` search backward |
| `;` / `,` | Repeat the last accepted character find in its original / opposite direction |
| `qtext` / `Qtext` | Through the entire next / previous literal phrase, ignoring case |

After `q`, typing `t` finds the next *t*. Typing `h` extends the query to *th*. Add `*2` to preview the second occurrence, then press **Enter** to accept. `Qth*2` selects through the second preceding *th*. Escape a literal asterisk with `\*`, as in `qprice\*2` to find the literal text `price*2`. Use `\\` for a literal backslash. Character finds such as `t-2` can cross paragraph or crumb boundaries.

## Find and replace

**Ctrl+F** starts at the caret and searches forward. **Ctrl+G** starts there and searches backward. In plain-text mode, typing immediately selects and centers the nearest match. Extending the query searches from the original caret. **F3** repeats in the opening direction and **Shift+F3** reverses it for one match.

**Ctrl+H** opens Find and Replace. Press it again to switch focus between **Find** and **Replace with**. Replacement is available in plain-text and regular-expression modes. **All…** previews the affected lines with before-and-after examples. **Apply all** makes the changes, and one Undo reverses them.

| Find and Replace controls | Action |
| --- | --- |
| Ctrl+P / Ctrl+R / Ctrl+L | Plain text / regular expression / number navigation for the current mode |
| Ctrl+M / Ctrl+W / Alt+W | Toggle match case / whole words / wrap |
| Alt+Down | Open and focus recent searches |
| Enter / Shift+Enter in the query | Continue in the opening / opposite direction |
| Escape, including from the manuscript | Close search and clear the text selection |

Search starts case-insensitive, with wrap enabled. Regular expressions prefer the shortest match by default. For example, `“.*”` finds one quoted passage on a line, and `(?-U)“.*”` allows a match through the last closing quote. Recent queries include their search options. [PCRE2 syntax](https://www.pcre.org/current/doc/html/pcre2syntax.html) · [Pattern guide](https://www.pcre.org/current/doc/html/pcre2pattern.html)

## Characters, Recurring, and spelling

Use **Ctrl+Shift+1** for **Search Characters** or **Ctrl+Shift+2** for **Search Recurring**. Type part of any synonym. The first matching group is highlighted and the other rows remain visible. **Up** and **Down** cycle through the matches. From the field or the list, **Enter** finds passages containing any alias in the highlighted group and **Ctrl+Enter** inserts its first alias at the manuscript caret. Activating a result clears the search field. **Escape** returns to the manuscript. **Insert** opens Add with the selected manuscript text suggested as one synonym. The suggestion is selected for editing or replacement. With an empty selection, the field opens blank. **Enter** saves the group. **F2** edits a group, and **Delete** removes it after confirmation.

On a compact Mac keyboard, **Control+I** opens Add while either the Characters or Recurring search field or list has focus. **Backspace** (the key labeled **Delete** on many Macs) removes the selected entry in a focused synonym, bookmark, or manuscript dictionary list. In a text field it deletes the preceding character.

The right panel follows the same search convention. **Ctrl+Shift+3** searches visits. **Ctrl+Shift+4** searches genres or titles. In either field, **Enter** or **Ctrl+Enter** activates the highlighted row and clears the search text. A visit or title opens its passage. A genre starts its next crumb.

In a sidebar, Bookmarks, or Insert Symbol, a filled search field shows a **Clear Search** button. From the keyboard, select all (**Ctrl+A**, or **Command+A** on a Mac) and press **Backspace** to clear the field and keep focus there.

Each group occupies one line in `Crumbs.txt.characters` or `Crumbs.txt.recurring`. The first alias determines the alphabetical order. For example, `"Fox, Captain", Red Fox, Captain Fox` has three aliases. Double quotes protect a comma inside an alias.

In the manuscript, a unique name completion appears faintly after three typed characters. Type `Inspector Goo` and press **F5** to accept the suggested ending of `Inspector Goose`. With several possibilities, **F5** opens a list. Arrow keys and **Enter** choose one, and **Escape** dismisses it. **Settings → Names** controls completion for each list independently (**Automatic**, **On demand**, **Off**). The minimum prefix length and underline color are adjustable. Underlining offers **All mentions**, **Current group only**, and **Off** separately for each list.

With the caret on or immediately after a name, **F6** finds the nearest earlier mention of any alias in its group. **Shift+F6** finds the next. A shared alias opens a group chooser. **Ctrl+Shift+J** or **Ctrl+click** follows a numbered reference such as `Cranolo 200` in Crumb mode, or follows a name backward in either mode. **Ctrl+Shift+click** follows a name forward. On a Mac, **Command+click** and **Command+Shift+click** also work. Jumps stop at the first or last mention. **Alt+Left** or **F2** returns to the typing position, even when the clicked word was several lines away. **Alt+Right** or **Shift+F2** moves forward again.

Whole names match regardless of case or curly versus straight apostrophes. A longer name covers any shorter alias inside the same occurrence. With both `Inspector Goose` and `Goose` listed, “Inspector Goose saluted a goose” has two occurrences. The results table, underlines, and navigation use this rule.

American English spelling is enabled by default. In Generic mode it checks ordinary prose throughout the document, including words on a line that happens to resemble a numbered crumb header. **F8** opens a correction with suggestions, Replace, Replace All, Ignore Once, Ignore All, and Add to Dictionary. **Ctrl+Shift+D** opens the manuscript’s dictionary. Each entry in `Crumbs.txt.dictionary` is one word, matched case-insensitively. Complete aliases in either list are accepted. A shorter name can be added as its own alias. Straight and curly apostrophes work in contractions. **Ctrl+,** opens Settings to switch spelling on or off.

## Statistics and daily count

**Ctrl+Shift+I** shows the number of recognized crumbs or nonblank paragraphs, the manuscript’s total words, and the average words per crumb or paragraph to two decimal places. **Ctrl+C** copies all three figures with their labels and locale-specific number separators. In Crumb mode, the daily counter tracks how many of today’s new crumbs remain. In Generic mode, it tracks the net change in words. Both persist per manuscript across sessions and start fresh on a new local date.

## Bookmarks and saves

**Ctrl+B** bookmarks the current physical line. Choose a category and give the bookmark a name, or press Enter to use the next number in the last category. **Alt+B** opens the filterable tree or list with each bookmark’s line and its crumb title or paragraph opening. A category with one bookmark opens that bookmark directly. Insert a paragraph above a bookmarked line 200 and the bookmark moves to 201. Delete the bookmarked line and its successor inherits the bookmark. At the end of the file it falls back to the last surviving line, or line 1 in an empty manuscript. Undo and redo move bookmarks with the edit.

A manuscript named `Crumbs.txt` keeps its bookmarks in `Crumbs.txt.bookmarks` in the same directory. Older bookmark files start at their recorded lines, up to the end of the manuscript. New saves include line and neighboring-text fingerprints so the editor can locate a passage after another program edits the manuscript. When a passage and its neighbors all disappear, the nearest valid stored line supplies a fallback. **Save As** carries the bookmarks, synonym lists, and personal dictionary beside the new manuscript.

Defaults include a timestamped backup beside the manuscript before editing and live saving in the background. Closing with pending changes offers **Save and Close**, **Discard Unsaved Changes**, and **Keep Editing**. Save and Close waits for the latest edit. A failed save keeps the window open.

[CrumbWriter.txt](CrumbWriter.txt) documents the file format, counting rules, keyboard controls, backup and recovery behavior, code structure, and maintenance checks.

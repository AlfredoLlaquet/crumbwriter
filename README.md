# Crumb Writer

<a href="https://github.com/AlfredoLlaquet/bsd-first"><img src="data/bsd-first.svg" width="100" alt="BSD first"></a>

Crumb Writer 3.0 is a keyboard-driven Qt 6 editor for FreeBSD, Linux, Windows, and macOS. On FreeBSD and Linux it runs in a graphical desktop such as GNOME or Xfce, or under a standalone window manager with X11 or a Wayland compositor. Use **Crumb mode** for a numbered collection of short pieces or **Generic mode** for a novel, an essay, or other paragraph-based prose. Both write plain UTF-8 without a byte order mark (BOM) and wrap the text to the full width of the writing area.

Programmed by **ChatGPT** for Alfredo Llaquet-Alsina.

## Download Crumb Writer

Choose the package for your operating system and processor. The filenames and links below all refer to **version 3.0**. For a newer edition, open the [latest release](https://github.com/AlfredoLlaquet/crumbwriter/releases/latest).

| Intended compatibility | Release checks | Download |
| --- | --- | --- |
| FreeBSD 15.x, amd64 | 15.1 | [crumbwriter-3.0-FreeBSD-15-amd64.pkg](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/crumbwriter-3.0-FreeBSD-15-amd64.pkg) |
| FreeBSD 14.x, amd64 | 14.4 and 14.5 | [crumbwriter-3.0-FreeBSD-14-amd64.pkg](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/crumbwriter-3.0-FreeBSD-14-amd64.pkg) |
| Ubuntu 24.04 LTS, amd64 | 24.04 | [crumbwriter_3.0-ubuntu24.04_amd64.deb](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/crumbwriter_3.0-ubuntu24.04_amd64.deb) |
| Ubuntu 26.04 LTS, amd64 | 26.04 | [crumbwriter_3.0-ubuntu26.04_amd64.deb](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/crumbwriter_3.0-ubuntu26.04_amd64.deb) |
| Debian 13, amd64 | 13 (trixie) | [crumbwriter_3.0-debian13_amd64.deb](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/crumbwriter_3.0-debian13_amd64.deb) |
| Windows 10 version 1903 or later and Windows 11, x64 | Windows build and desktop audit on the hosted Windows runner | [CrumbWriter-3.0-Windows-x64-Setup.exe](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/CrumbWriter-3.0-Windows-x64-Setup.exe) |
| macOS 15 or later, Apple silicon and Intel | macOS 15 on both CPU architectures | [CrumbWriter-3.0-macOS-Universal.dmg](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/CrumbWriter-3.0-macOS-Universal.dmg) |

The FreeBSD package matches the major version. Within that major, the required Qt libraries must be available from your configured package repository. Both Ubuntu LTS packages target their respective release series, including point updates. Debian has its own package built against Debian’s libraries. Other Linux distributions with Qt 6 can build the editor from source. Windows 11 on Arm is expected to run the x64 installer through emulation. The Windows and Mac downloads bundle Qt, American English spelling, and an offline thesaurus. On FreeBSD, Ubuntu, and Debian the package manager installs them.

### License and warranty

Crumb Writer’s original code is dedicated to the public domain. Anyone may use, copy, modify, sell, or redistribute it. The software is provided “as is,” without warranty. To the fullest extent permitted by applicable law, the authors disclaim liability for claims or damages arising from its use. The BSD first badge has a separate CC0 dedication, included with the app. Writers retain their rights to their manuscripts. [Full text of the Unlicense](LICENSE)

The bundled libraries, spelling dictionary, and WordNet thesaurus data have their own licenses. The Windows installation folder and the Mac app’s `Contents/Resources` folder contain `third-party/README.txt` and the individual notices. Corresponding library sources and build recipes are available alongside the installers on each release page. Inside the editor, **About Crumb Writer → View License** opens the full Crumb Writer license offline.

### Install and update on FreeBSD

**First installation.** Download the FreeBSD package above for a 64-bit Intel or AMD computer running FreeBSD 15.x or 14.x. From the directory containing the download, run the following as root. The leading `./` tells `pkg` to install the file you downloaded. It obtains dependencies from your configured FreeBSD repositories.

~~~sh
pkg install ./crumbwriter-3.0-FreeBSD-15-amd64.pkg
~~~

On FreeBSD 14.x, use `crumbwriter-3.0-FreeBSD-14-amd64.pkg` in the install command.

Open the desktop application launcher, type **Crumb Writer**, and press **Enter**. Press **Ctrl+O** to open a manuscript. On subsequent launches the editor reopens the last one. **F10**, then **A**, displays the installed version.

**Update.** Save your writing and close the editor. Download the newer package from the latest release and repeat the install command with the new version in its filename. The package name is `crumbwriter`. To remove the program, run `pkg delete crumbwriter`. Manuscripts and their companion files are yours and stay in place.

### Install and update on Ubuntu or Debian

**First installation.** Choose the package corresponding to the Ubuntu or Debian version installed on your 64-bit Intel or AMD computer. The example uses Ubuntu 24.04 LTS. In the directory containing the file, run the following commands. APT installs Crumb Writer and its dependencies, including the US English spelling and thesaurus data.

~~~sh
sudo apt update
sudo apt install ./crumbwriter_3.0-ubuntu24.04_amd64.deb
~~~

For Ubuntu 26.04, use `crumbwriter_3.0-ubuntu26.04_amd64.deb` in the command. On Debian 13, use `crumbwriter_3.0-debian13_amd64.deb`.

Open **Crumb Writer** from the desktop application menu. Use **Ctrl+O** to open a file and **F1** for the searchable keyboard guide.

**Update.** Close the editor, download the newer `.deb`, and run `sudo apt install ./` followed immediately by the new filename. Each new version comes from the GitHub release page. To uninstall the app, run `sudo apt remove crumbwriter`.

**Switching from a source build.** A previous Ubuntu `sudo make install` normally placed an executable in `/usr/local/bin`. If the launcher still opens that copy, check `command -v crumbwriter`. When it reports `/usr/local/bin/crumbwriter`, close the editor and rename that old executable with `sudo mv /usr/local/bin/crumbwriter /usr/local/bin/crumbwriter-source`. The packaged executable is `/usr/bin/crumbwriter`. A personal installation in `~/.local/bin` can take precedence the same way.

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

GitHub Actions creates a signed build record, called an **artifact attestation**, for every downloadable package and the checksum list. With GitHub CLI installed and signed in, run the command for the file just downloaded, in the folder containing it.

~~~text
gh attestation verify crumbwriter-3.0-FreeBSD-15-amd64.pkg --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter-3.0-FreeBSD-14-amd64.pkg --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter_3.0-ubuntu26.04_amd64.deb --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter_3.0-debian13_amd64.deb --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter_3.0-ubuntu24.04_amd64.deb --repo AlfredoLlaquet/crumbwriter
gh attestation verify CrumbWriter-3.0-Windows-x64-Setup.exe --repo AlfredoLlaquet/crumbwriter
gh attestation verify CrumbWriter-3.0-macOS-Universal.dmg --repo AlfredoLlaquet/crumbwriter
~~~

A successful check identifies the repository, build workflow, and source revision that produced the downloaded bytes. Windows SmartScreen and Apple Gatekeeper make their own decisions when opening the program. [GitHub CLI verification guide](https://cli.github.com/manual/gh_attestation_verify)

For a quick integrity check, download the checksum list for the same release and compare the 64 hexadecimal characters beside your filename with your local result. On FreeBSD, run `sha256 -q crumbwriter-3.0-FreeBSD-15-amd64.pkg`. On Ubuntu, run `sha256sum crumbwriter_3.0-ubuntu24.04_amd64.deb`. On macOS, run `shasum -a 256 CrumbWriter-3.0-macOS-Universal.dmg`. In Windows PowerShell, use `Get-FileHash .\CrumbWriter-3.0-Windows-x64-Setup.exe -Algorithm SHA256`. [Version 3.0 checksums](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.0/SHA256SUMS)

The repository uses GitHub’s **immutable releases**. When a release is published with this setting, GitHub locks the tag to its source commit and locks the attached files. The signed build record independently lets a reader verify the downloaded bytes. [GitHub’s immutable release guide](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)

## Build and run on FreeBSD

### First installation

For a source installation on FreeBSD 14.x or 15.x, install the dependencies as root.

~~~sh
pkg install git pkgconf glib qt6-base hunspell en-hunspell en-mythes
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

Open the desktop application launcher, type **Crumb Writer**, and press **Enter**. **Ctrl+O** opens a manuscript and places the caret at the end. The next launch normally reopens the last manuscript. **F1** opens the searchable keyboard reference.

For a personal installation, run `make install PREFIX="$HOME/.local"` as your desktop account. That account’s graphical session needs `$HOME/.local/bin` on its `PATH` so the launcher can find the executable.

### Update

When updating a source build from 2.5.1 or earlier, install the new thesaurus data as root with `pkg install en-mythes`. Save your manuscript and close Crumb Writer. Update the existing checkout as your desktop account.

~~~sh
cd crumbwriter
git pull --ff-only
~~~

In the updated directory, repeat the original installation command and prefix. For a system installation, run `make install` as root. Reopen **Crumb Writer** from the application menu. **F10**, then **A**, opens About, which shows the installed version.

## Build and run on Ubuntu

The release includes native Ubuntu 24.04 LTS, Ubuntu 26.04 LTS, and Debian 13 packages. From a terminal on the desktop account, install the build tools, Qt 6, GLib, Hunspell, and the American English spelling and thesaurus dictionaries, then build and install the desktop application.

~~~sh
sudo apt update
sudo apt install git build-essential pkgconf qt6-base-dev qt6-wayland libglib2.0-dev libhunspell-dev hunspell-en-us mythes-en-us libxcb-cursor0
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
sudo make install
~~~

Open the desktop application menu, search for **Crumb Writer**, and press **Enter**. For a source update from 2.5.1 or earlier, first run `sudo apt install mythes-en-us`. For subsequent updates, close the application, run `git pull --ff-only` in the checkout, and run `sudo make install` again. On either platform, `make` as your desktop account can perform the compilation ahead of the system installation. Run `make check` in a graphical session for the parser, storage, and editor tests. [Unix package, desktop, and tiling checks](https://github.com/AlfredoLlaquet/crumbwriter/actions/workflows/check.yml)

## Writing Crumbs in Crumb Mode

A crumb is a short piece with three fields on one physical file line. The first colon ends the **header**, and the second ends the **title**. The rest of the line is the **body**, which can contain additional colons. A header consists of the manuscript’s **label**, a space, and the **number** of the piece. Start at 1 so that crumb 200 occupies line 200. Title and body may both be empty.

The first file line does double duty. It begins with the title of the whole work, immediately followed by `//` and the first crumb. Give each manuscript a distinctive label. In the following example the work is *Cranolos - Manolo’s Crumbs*, its label is `Cranolo`, and the first piece is *The Inspector*.

~~~text
Cranolos - Manolo’s Crumbs//Cranolo 1: The Inspector: A goose audits the moon for bumps.
Cranolo 2: Night Audit #1: The tide submits its receipts.
Cranolo 3: Night Audit #2: One receipt smells of salt.
~~~

Here the third piece has header number **3** and genre number **2**. The header number locates a piece in the manuscript. A sequence such as *Night Audit* can recur anywhere in the collection and has its own numbering. Type `Night Audit #` in a later title to continue from the nearest earlier occurrence. For a new entry at the end, press **Ctrl+Shift+4**, type part of the genre, and press **Enter**. The editor supplies the next header and title and leaves the caret ready for the body. **Ctrl+N** appends a crumb with an empty title. Bold red digits flag a mismatch with the file line or a break in a genre’s sequence. **F4** visits the next warning.

On screen the header is italic, the title is bold, and the body uses the chosen writing font. A long piece may cross many screen rows while keeping its one file line. **Tab** moves between titles and bodies, **Ctrl+Down** moves to the next crumb, and **Ctrl+Q** selects and copies the current piece. The work title stays outside that selection. A new crumb enters Visit History immediately, and its entry updates as you write its title. A visit appears under the piece’s own title, so two pieces with the same title can still be revisited independently. To start a collection in a blank editor, type the work title followed immediately by `//`, then press **Ctrl+N** to create the first header on that line. Change its default label, `Crumb`, to the collection’s own label once. The next header will reuse it.

A publisher can use those same field boundaries to present a piece on the web. The official Crumb Reader fetches Alfredo Llaquet-Alsina’s published `Crumbs.txt`, checks each `Crumb` number against its line, and separates its title and body. In the manuscript, the first title after `Crumb 1:` is *The Game*. Its direct link opens that piece in the Reader, where the reader can move on, search the collection, or mark a favorite. The source line, parser, and reading view show the passage from manuscript to publication. [Published manuscript, line 1](https://github.com/AlfredoLlaquet/Alfredo_Llaquet-Alsina_Crumbs/blob/main/Crumbs.txt#L1) · [Reader parser](https://github.com/AlfredoLlaquet/Alfredo_Llaquet-Alsina_Crumbs/blob/main/Reader/core.js) · [Read *The Game*](https://www.thecrumbs.org/Reader/?crumb=1)

## Writing Novels and Other Prose in Generic Mode

Generic mode gives a novelist one readable file that can serve as the draft, the edited manuscript, and the source for publication. Press **Enter** when a paragraph ends. A long paragraph wraps to the available width as you write. Drag the window wider or narrower and the words reflow on screen while the paragraph keeps its physical file line. Put consecutive paragraphs on consecutive lines. A blank line can mark a pause or scene break wherever your writing calls for one.

Consider these first six lines of a manuscript named `Harbor.txt`.

~~~text
The Harbor of Borrowed Tides

The Night Clerk
---------------
At dusk the harbor clerk signed for a tide that had arrived in six wooden crates.
By dawn the water had learned the shape of the pier.
~~~

**Giving the document a title.** A nonblank first line appears in the largest bold type when it contains no period, its letters are all uppercase, it is immediately followed by a blank line, or it is immediately followed by a heading marker. Any one of these is enough. A marker lets you title a book *Mrs. Dalloway* even when the next paragraph follows immediately. The first blank directly beneath a recognized general title is a compact spacer.

**Organizing a book.** Write the heading on its own line. On the following line, type one of these characters at the left edge.

| Marker | Level | An example use |
| --- | --- | --- |
| `=` | 1 | Part |
| `-` | 2 | Chapter |
| `~` | 3 | Section |
| `.` | 4 | Subsection |

One character is enough. Leave the marker line to confirm it. The editor repeats its character to match the heading’s visible length, including spaces. A heading of *The Night Clerk* takes 15 characters. Accents and emoji count as the characters you see. An existing line of hyphens continues to mark a level-two heading. A provisional marker stays editable through live saving, so you can write `- ` and continue with `Buy coffee` on the same line. A marker contains only repetitions of its own character. An ordinary sentence ending in a period stays prose.

Heading levels have progressively smaller type and solid or dotted underlining. Before a section, extra space on screen marks the transition. Off the caret a marker takes up a sliver of space. Put the caret on the marker to edit it at normal size. **Show Manuscript Markup** reveals all markers and italic delimiters together. Elsewhere in the manuscript, an empty line has ordinary line height and two empty lines leave two lines of space. A paragraph surrounded by blank lines stays prose. Surplus blank lines at the end are trimmed when a file opens or saves. While you type, a new blank writing line stays available through autosave. A saved nonempty manuscript has one final line ending. An empty manuscript saves as an empty file.

**Writing in italics.** Enclose a passage in guillemets, for example `«the other moon»`. The delimiters appear while you edit the passage and the words appear in italics when you move away. **Ctrl+K** inserts the opening or closing mark. With a selection it wraps or unwraps the passage in one undo step. Each pair belongs to one paragraph. In manuscripts that use guillemets for dialogue, choose **Manuscript Settings → Display guillemets literally**. The same dialog lets you **Disable general title detection**. This makes the first paragraph ordinary prose unless it has an explicit heading marker. These choices travel with the manuscript in a plain-text companion such as `Harbor.txt.config`.

**Writing lists.** A new paragraph after `1. Buy coffee` starts with `2. `. Continue a bullet, a lettered list, a Roman sequence, or a multipart sequence such as `1.9.` → `1.10.`. An empty generated item ends the list on **Enter**. Immediate **Backspace** removes the whole suggested prefix. You can type over any prefix. An ordinary sentence beginning with *I* remains prose. For an ambiguous first item, use **Continue List** (**Ctrl+Shift+Enter**). Opening or pasting a manuscript preserves its existing numbering.

**Finding your place.** Use **Ctrl+Up** and **Ctrl+Down** for physical lines. **Tab** and **Shift+Tab** jump between headings. **Ctrl+Shift+4** focuses **Titles**. Type part of a title and press **Enter** to go there. **Shift+F7** switches between manuscript order and alphabetical order. Manuscript order shows a tree. Alphabetical order shows a flat index with parent headings for context. As you write, the outline selects the heading that contains the caret. Activating the general title places it at the top of the writing area. **End of document** is always the final entry in either order. It takes you to the end of the last actual paragraph. **Ctrl+Shift+3** searches visited paragraphs by their full text while showing their opening words. The right panel displays the day’s net word count, so deleting 150 more words than you add shows −150.

**Using the manuscript outside the editor.** To find the passage about the crates, a reader or script can run the following command.

~~~sh
grep -nF 'six wooden crates' Harbor.txt
~~~

The result starts with `5:At dusk` and includes the full paragraph. A revision comparison can show a change to that paragraph on one line. A backup contains the words, accents, curly apostrophes, and em dashes you typed. A script can identify a chapter by looking one line ahead for a line of repeated `=`, `-`, `~`, or `.` characters. These conventions use ordinary UTF-8 text and are simple enough to follow by eye in another editor.

The same source can supply a print edition, a website, or a reflowable ebook. A publishing script can turn the book title into an HTML `<h1>`, *The Night Clerk* into an `<h2>`, and the two prose lines into separate `<p>` elements. Those editions can choose their own margins, typefaces, and page sizes. Correct the crates sentence once in `Harbor.txt` and the next build of each edition can pick it up. Years later, the manuscript remains readable independently of those tools.

**Mode follows the manuscript.** A `//` delimiter outside a URL on the first physical line activates Crumb mode. A first line without that delimiter, including an empty line or an ordinary web address such as `https://www.thecrumbs.org/`, activates Generic mode. An older collection that starts directly with `Crumb 1:` can activate Crumb mode by adding the work title and `//` before its first header. Editing that line switches the mode immediately. The right end of the status bar shows the current mode.

## How Words Are Counted

A crumb’s count includes everything from the header label through the end of its body. Count each run between spaces or other word-separating whitespace. An em dash also separates words. A slash with an ordinary space immediately on each side acts as a separator. Hyphens, apostrophes, and punctuation attached to a word stay with it. For example, `moon-lit / sand—bank` counts as three words. A separate punctuation token counts too, so `Crumb 2: :` has three words.

The current-crumb panel skips the general work title on line 1. The **whole-manuscript total includes it**. In the Cranolos example the first crumb has 11 words. Its full file line has 15 because `Cranolos - Manolo’s Crumbs` contributes four, including the separately spaced hyphen. The average divides the whole-file total by the number of recognized crumbs.

The official reference on FreeBSD and Linux is the following command. It also runs on macOS. On line 1, the first substitution replaces the first literal `//` with a single space. On every line, the remaining substitutions replace each em dash and each nonoverlapping literal ` / ` with a space. Finally, `wc -w` counts the resulting words. For the first **individual** crumb, remove the work title through the actual delimiter and apply the last two substitutions to the rest of that line.

~~~sh
sed -e '1s@//@ @' -e 's/—/ /g' -e 's@ / @ @g' Crumbs.txt | wc -w
~~~

The exact count for unusual Unicode characters depends on the platform’s `wc -w`. FreeBSD and macOS check the remaining UTF-8 bytes individually. On Ubuntu, GNU `wc` reads whole characters in a UTF-8 locale. The Windows build reproduces FreeBSD 15.x’s `C.UTF-8` result, independent of the Windows display language. [FreeBSD implementation](https://github.com/freebsd/freebsd-src/blob/releng/15.0/usr.bin/wc/wc.c) · [Apple implementation](https://github.com/apple-oss-distributions/text_cmds/blob/main/wc/wc.c) · [GNU word counting](https://www.gnu.org/software/coreutils/manual/html_node/wc-invocation.html)

Generic prose uses the same whitespace, em dash, and spaced slash rules. A URL keeps its slashes. Manuscript Statistics includes headings among the nonblank paragraphs and excludes blank lines and structural heading markers from the word and paragraph totals and the average.

## Working with several manuscripts

**Ctrl+O** opens a file in a tab. Opening the same file again activates its existing tab, even if you moved it to another window. Switch tabs with **Ctrl+Tab** and **Ctrl+Shift+Tab**. The panels follow the active manuscript. Each manuscript keeps its own undo history, search, bookmarks, spelling dictionary, selection, and scroll position. **Ctrl+T** opens a blank tab. **Ctrl+Shift+N** opens another window. For a second monitor, **Move Tab to New Window** takes the manuscript and its editing history with it.

**Ctrl+F4** closes a tab. **Ctrl+Shift+T** reopens the most recently closed saved manuscript at its previous position. **Alt+W** closes the window. A window containing pending saves identifies the affected manuscripts before closing. In the writing area, **Ctrl+W** still deletes a word and **Ctrl+N** still appends a crumb or paragraph.

## Writing and navigation

On a Mac, **Ctrl** means the physical **Control** key and **Alt** means **Option**. The usual **Command** shortcuts also work for New Window, Open, Save, Save As, Find, Select All, Cut, Copy, Paste, Undo, and Redo. **Command+,** opens Settings, **Command+W** closes the current window, and **Command+Q** quits the editor through its normal save confirmation. **Control+Shift+Space** starts movement selection when **Control+Space** switches input sources. **Control+Shift+B** opens Bookmarks, and **Control+Shift+W** toggles Wrap in Find, leaving Option available for typing special characters. On keyboards whose top row controls media, hold **Fn** for function keys such as **F1**.

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Append a crumb and start its body, or start a new paragraph at the end in Generic mode |
| Ctrl+1 | In Crumb mode, select the header and copy it in lowercase, such as `crumb 42` |
| Ctrl+2 | Select the word at the caret, including internal apostrophes and hyphens |
| F7 | Look up the selected words or the word at the caret in the offline English thesaurus |
| Ctrl+Shift+L, then D / S / T | Open the Merriam-Webster dictionary entry / its synonyms section / the thesaurus entry |
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
| F6 / Shift+F6 | Previous / next mention of a character, recurring term, or crumb genre |
| Ctrl+Shift+J / Ctrl+click | Open the web link, follow the numbered crumb reference, or find the previous mention at the caret |
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

A faint tint of your desktop accent follows the current crumb or paragraph across all its wrapped lines in both light and dark themes. When another window becomes active, the writing area changes to a softer gray. Visiting a sidebar or the search field keeps the manuscript window active. The manuscript font and size can also be changed in Settings. The middle of the status bar links to **Read Alfredo Llaquet-Alsina’s Crumbs at TheCrumbs.org**. In a narrow window or during a movement command, the writing status takes that space. The website is also available through **F10 → Open TheCrumbs.org**.

Smart punctuation writes curly quotes and apostrophes, em dashes, and ellipses into the manuscript as typing supplies context. Paste cleans tabs, repeated spaces, and trailing spaces in one undoable operation. Backspacing over the last word leaves its preceding space at the caret for the replacement word, even through an autosave. Saved files omit trailing spaces and use UTF-8 without a BOM.

Consecutive typing forms one undo step, including spaces, pauses, automatic punctuation, and live saves. Consecutive Backspaces and consecutive forward Deletes each form their own step. Moving the caret, pressing Enter, pasting, or switching between typing and deletion starts another step.

**Settings → Writing → Copy deleted text to the clipboard** starts off. Turn it on to collect the passage removed by deleting a selection, using **Ctrl+W** on a word, or deleting a whole crumb with **Ctrl+Y**. Backspacing through `goose` one character at a time leaves your previous clipboard entry available for pasting. Holding down Delete does the same. An explicit **Cut** always copies. With deletion copying enabled, pasting over a selection puts the displaced passage on the clipboard, and Replace All collects the displaced passages in manuscript order. Your existing preference is honored when you upgrade.

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

The right panel follows the same search convention. **Ctrl+Shift+3** searches visits. **Ctrl+Shift+4** searches genres or titles. A visit or title opens its passage. In **Genres**, press **Enter** to write the next crumb in the highlighted genre, or **Ctrl+Enter** to see its occurrences in the manuscript.

In a sidebar, Bookmarks, or Insert Symbol, a filled search field shows a **Clear Search** button. From the keyboard, select all (**Ctrl+A**, or **Command+A** on a Mac) and press **Backspace** to clear the field and keep focus there.

Each group occupies one line in `Crumbs.txt.characters` or `Crumbs.txt.recurring`. The first alias determines the alphabetical order. For example, `"Fox, Captain", Red Fox, Captain Fox` has three aliases. Double quotes protect a comma inside an alias. In Add or Edit, check **Exclude from autocomplete** to suppress suggestions for this particular group, including on-demand **F5** suggestions. Its names still work for underlining, spelling, navigation, searching, and deliberate insertion with **Ctrl+Enter**. The option travels with the manuscript’s companion file.

In the manuscript, a unique name completion appears faintly after three typed characters. Type `Inspector Goo` and press **F5** to accept the suggested ending of `Inspector Goose`. With several possibilities, **F5** opens a list. Arrow keys and **Enter** choose one, and **Escape** dismisses it. **Settings → Names** controls completion for each list independently (**Automatic**, **On demand**, **Off**). The minimum prefix length and underline color are adjustable. Underlining offers **All mentions**, **Current group only**, and **Off** separately for each list.

With the caret on or immediately after a name, **F6** finds the nearest earlier mention of any alias in its group. **Shift+F6** finds the next. A shared alias opens a group chooser. **Ctrl+Shift+J** or **Ctrl+click** follows a numbered reference such as `Cranolo 200` in Crumb mode, or follows a name backward in either mode. **Ctrl+Shift+click** follows a name forward. On a Mac, **Command+click** and **Command+Shift+click** also work. Jumps stop at the first or last mention. **Alt+Left** or **F2** returns to the typing position, even when the clicked word was several lines away. **Alt+Right** or **Shift+F2** moves forward again.

Whole names match regardless of case or curly versus straight apostrophes. A longer name covers any shorter alias inside the same occurrence. With both `Inspector Goose` and `Goose` listed, “Inspector Goose saluted a goose” has two occurrences. The results table, underlines, and navigation use this rule. Each result shows the exact matched text in bold inside its surrounding passage. In Crumb mode, a genre such as *Night Audit* also works as a name wherever you mention it in the text. Genres follow the Recurring underline preference and are available for navigation and occurrence searches. The genre list itself supplies no typing suggestions.

American English spelling is enabled by default. In Generic mode it checks ordinary prose throughout the document, including words on a line that happens to resemble a numbered crumb header. **F8** opens a correction with suggestions, Replace, Replace All, Ignore Once, Ignore All, and Add to Dictionary. **Ctrl+Shift+D** opens the manuscript’s dictionary. Each entry in `Crumbs.txt.dictionary` is one word, matched case-insensitively. Complete aliases in either list are accepted. A shorter name can be added as its own alias. Straight and curly apostrophes work in contractions such as `I’m`. Adding `Hedberg` also accepts both `Hedberg’s` and `Hedberg's`. Web addresses are excluded from spelling review. **Ctrl+,** opens Settings to switch spelling on or off.

### Links, word selection, and dictionaries

A recognized web address is underlined. **Ctrl+click** opens it in your browser (**Command+click** also works on a Mac). With the caret on the address, press **Ctrl+Shift+J**, or use **Open Link** in the editor’s context menu. An address starting with `www.` opens using HTTPS. A sentence-ending comma or period stays in the prose. A closing parenthesis that belongs to the address stays with the link.

Double-click or press **Ctrl+2** to select a word. In `notwithstanding—not` either word can be selected separately, and clicking the em dash selects the dash itself. Internal punctuation stays with words such as `O’Brien` and `well-read`. To look up an ordinary English synonym while drafting, press **F7**. The offline thesaurus uses the selection, or the word under the caret, and groups alternatives by meaning. Use the arrows to choose a word. **Enter** replaces the original passage in one undoable edit, **Ctrl+C** copies the chosen alternative, and **Escape** closes the window. **Ctrl+L** lets you try another lookup. For example, look up `doctor` and choose the sense you intend before picking an alternative.

For Merriam-Webster in your browser, select a phrase or place the caret on a word. Press **Ctrl+Shift+L**, release the keys, then press **D** for the dictionary, **S** for the entry’s synonyms section, or **T** for the thesaurus. **Escape** cancels the lookup. The menu equivalents are under **F10 → Edit**. Only the web commands send the chosen word or phrase to the website.

The spelling checker recognizes complete genre names in Crumb mode, including multiword names, alongside Characters and Recurring. Typing suggestions use 30% of the writing color over the actual paragraph background in either theme. The full suggestion list remains available with **F5**.

## Statistics and daily count

**Ctrl+Shift+I** shows the number of recognized crumbs or nonblank paragraphs, the manuscript’s total words, and the average words per crumb or paragraph to two decimal places. **Ctrl+C** copies all three figures with their labels and locale-specific number separators. In Crumb mode, the daily counter tracks how many of today’s new crumbs remain. In Generic mode, it tracks the net change in words. Both persist per manuscript across sessions and start fresh on a new local date. The Generic panel puts the grouped word count on its own line. To correct a starting figure, open **F10 → Set Today’s Count**, enter the desired value, and press **Enter**. **Reset to Zero** fills in zero for you to confirm. Generic mode accepts negative figures. Subsequent additions, deletions, and undo operations continue from the figure you set.

## Bookmarks and saves

**Ctrl+B** bookmarks the current physical line. Choose a category and give the bookmark a name, or press Enter to use the next number in the last category. **Alt+B** opens the filterable tree or list with each bookmark’s line and its crumb title or paragraph opening. A category with one bookmark opens that bookmark directly. Insert a paragraph above a bookmarked line 200 and the bookmark moves to 201. Delete the bookmarked line and its successor inherits the bookmark. At the end of the file it falls back to the last surviving line, or line 1 in an empty manuscript. Undo and redo move bookmarks with the edit.

A manuscript named `Crumbs.txt` keeps its bookmarks in `Crumbs.txt.bookmarks` in the same directory. Older bookmark files start at their recorded lines, up to the end of the manuscript. New saves include line and neighboring-text fingerprints so the editor can locate a passage after another program edits the manuscript. When a passage and its neighbors all disappear, the nearest valid stored line supplies a fallback. **Save As** carries the bookmarks, synonym lists, personal dictionary, and manuscript settings beside the new manuscript.

**Compare with Startup Backup** opens a read-only view of the paragraphs that have changed since this file was opened. The backup appears on the left and your current text on the right. Use **F3** and **Shift+F3** to move through changes, **F5** to compare again after editing, or **Escape** to close the comparison.

Defaults include a timestamped backup beside the manuscript before editing and live saving in the background. Closing with pending changes offers **Save and Close**, **Discard Unsaved Changes**, and **Keep Editing**. Save and Close waits for the latest edit. A failed save keeps the window open.

[CrumbWriter.txt](CrumbWriter.txt) documents the file format, counting rules, keyboard controls, backup and recovery behavior, code structure, and maintenance checks.

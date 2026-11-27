<table>
  <tr>
    <td width="128" valign="middle">
      <a href="https://github.com/AlfredoLlaquet/bsd-first">
        <img src="data/bsd-first.svg" alt="BSD first" width="128">
      </a>
    </td>
    <td valign="middle">This application was built and tested on BSD first to make it run well on as many operating systems as possible.</td>
  </tr>
</table>

# Crumb Writer

**Crumb Writer lets you write with the ease of a word processor and saves your work directly as a plain text file.** If you’re at home in Microsoft Word, LibreOffice Writer, Google Docs, or Apple Pages, the experience will feel familiar. Choose a typeface you enjoy reading. Your headings stand out, your italics show, and your lists continue as you type. Open the same `.txt` file in any text editor and your whole document is there.

That choice pays off long after the first draft. Your files stay small. You can search a folder full of documents for a half-remembered sentence or compare two drafts to see exactly which words changed. A publishing tool can use your chapter headings to build an ebook’s table of contents or turn each chapter into a page on a website. When you revise a sentence, it’s already in the source for the next edition. A separate export from DOCX or ODT is a step you can skip.

Crumb Writer is free for FreeBSD, OpenBSD, Linux, Windows, and macOS.

Created and directed by **Alfredo Llaquet-Alsina**. Implemented by **ChatGPT**.

## Contents

- [Writing prose](#from-a-page-of-notes-to-a-whole-book) · [Finding words](#find-the-word-you-mean) · [Tracking characters and places](#keep-track-of-the-world-inside-your-writing)
- [Document files](#what-travels-with-your-document) · [Writing your own crumbs](#write-your-own-crumbs)
- [Downloads and installation](#download-crumb-writer) · [Verifying a download](#verify-a-download)
- [Building on FreeBSD](#build-and-run-on-freebsd) · [Building on OpenBSD](#build-and-run-on-openbsd) · [Building on Ubuntu](#build-and-run-on-ubuntu)
- [Import and export](#import-and-export) · [Plain text file format](#plain-text-file-format) · [Crumb mode](#writing-crumbs-in-crumb-mode) · [Generic mode](#writing-novels-and-other-prose-in-generic-mode)
- [Word counts](#how-words-are-counted) · [Tabs and sessions](#working-with-several-documents) · [Writing and navigation](#writing-and-navigation)
- [Find and replace](#find-and-replace) · [Names and spelling](#characters-recurring-and-spelling) · [Statistics](#statistics-and-daily-count) · [Bookmarks and saves](#bookmarks-and-saves) · [Licenses](#licenses-and-credits)

## From a page of notes to a whole book

**Generic mode** is where you write novels, short stories, memoirs, essays, and articles. The same tools suit a lecture series, a speech, a research journal, interview notes, meeting minutes, a project proposal, a software specification, a technical manual, or the checklist you need this afternoon.

Type a chapter name, put a single hyphen on the line below it, and move on. The name becomes a heading on screen and appears in the **Titles** outline. Four heading levels give a long work room for parts, chapters, sections, and subsections. Each paragraph wraps to the width of the window and stays on one line in the file, so a program preparing your book for publication can tell exactly where the paragraph ends.

Select the word *borrowed* in “I borrowed the moon” and press **Ctrl+K**. On screen you read “I *borrowed* the moon.” In the file, `«borrowed»` preserves the emphasis. Lists are just as direct. Press **Enter** after a numbered item and the next number is ready for you. Bullets, letters, Roman numerals, and multilevel outlines continue as you write, right down to `1.9.` becoming `1.10.`. Every item remains ordinary editable text.

## Find the word you mean

American English spell check flags possible mistakes as you write, suggests corrections, and remembers the words you add to the document’s own dictionary. When a word is spelled perfectly and still feels wrong, **F7** opens an offline thesaurus. Alternatives are grouped by meaning, and **Enter** puts your choice straight into the sentence.

For a closer look, put the cursor on a word or select a phrase. Press **Ctrl+Shift+L**, release the keys, then press **D** for its Merriam-Webster dictionary entry, **S** for that entry’s synonyms section, or **T** for the thesaurus. Your browser opens at the reference you chose, with the word already looked up.

## Keep track of the world inside your writing

Suppose chapter one introduces *Mara Vale*, chapter eight calls her *the night clerk*, and halfway through chapter sixteen you want to reread her last appearance. Give Mara one entry in **Characters** with both names. From either name, **F6** takes you to the previous mention under either identity. You can also see every occurrence with the surrounding passage. **Alt+Left** brings you back to the sentence you were writing.

**Recurring** does the same for places, objects, organizations, and ideas. In a software specification, *emergency stop* and *E-stop* can belong to one entry. In a novel, *the spare key* and *the brass key* can belong to another. Both lists can quietly underline familiar terms and suggest their completion as you type.

**Titles** shows which section you’re in and lets you jump to another. **Visit History** brings back the paragraph you were editing a few minutes ago. For a place you want to find again next month, give it a **bookmark** with a category and a name, such as **Revision → Who gave Mara the key?** The bookmark follows the passage when you add or remove paragraphs above it.

## What travels with your document

The writing itself lives in one file. As you use the editor’s other tools, Crumb Writer keeps their information in readable companion files beside it. For a document called `Harbor.txt`, these are the files you’ll see.

| Companion file | What it keeps |
| --- | --- |
| `Harbor.txt.characters` | People and the names they go by |
| `Harbor.txt.recurring` | Recurring subjects and their alternate names |
| `Harbor.txt.bookmarks` | Named places to return to, organized by category |
| `Harbor.txt.dictionary` | Words you want the spelling checker to recognize in this document |
| `Harbor.txt.config` | This document’s preferences, such as whether guillemets mark italics or dialogue |

Send `Harbor.txt` to someone who wants to read it. Take the companions along when you move to another computer and want your names, vocabulary, and bookmarks to come with you. By default, Crumb Writer saves as you work and makes a dated backup when you open an existing document.

## Write your own crumbs

A crumb is a short literary piece. It might be a story, a poem, a joke, or an observation that says everything it needs to in a single sentence. Alfredo Llaquet-Alsina created the form and encourages everyone to write crumbs of their own. Your collection can have its own voice, its own recurring genres, and even its own word for a crumb.

In **Crumb mode**, press **Ctrl+N** to start your next piece. Each crumb has a numbered header, a title, and a body. It occupies one line in the plain text file and wraps comfortably on screen. The side panel shows its title and word count, and the **Genres** list keeps track of any series you develop. If your last haiku was *Haiku #17*, choose *Haiku* and Crumb Writer starts *Haiku #18*, even if it happens to be crumb 250 in your collection.

Alfredo’s collection is one example of what that plain text file can become. It powers the Crumb Reader at TheCrumbs.org, where readers can browse, search, and share a link to any individual piece. [Read the first crumb](https://www.thecrumbs.org/Reader/?crumb=1)

## Download Crumb Writer

Choose the package for your operating system and processor. The filenames and links below all refer to **version 3.1**. For a newer edition, open the [latest release](https://github.com/AlfredoLlaquet/crumbwriter/releases/latest).

| Intended compatibility | Release checks | Download |
| --- | --- | --- |
| FreeBSD 15.x, amd64 | 15.1 | [crumbwriter-3.1-FreeBSD-15-amd64.pkg](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/crumbwriter-3.1-FreeBSD-15-amd64.pkg) |
| FreeBSD 14.x, amd64 | 14.4 and 14.5 | [crumbwriter-3.1-FreeBSD-14-amd64.pkg](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/crumbwriter-3.1-FreeBSD-14-amd64.pkg) |
| OpenBSD 8.0, amd64 | 8.0 | [crumbwriter-3.1.tgz](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/crumbwriter-3.1.tgz) |
| Ubuntu 24.04 LTS, amd64 | 24.04 | [crumbwriter_3.1-ubuntu24.04_amd64.deb](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/crumbwriter_3.1-ubuntu24.04_amd64.deb) |
| Ubuntu 26.04 LTS, amd64 | 26.04 | [crumbwriter_3.1-ubuntu26.04_amd64.deb](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/crumbwriter_3.1-ubuntu26.04_amd64.deb) |
| Debian 13, amd64 | 13 (trixie) | [crumbwriter_3.1-debian13_amd64.deb](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/crumbwriter_3.1-debian13_amd64.deb) |
| Windows 10 version 1903 or later and Windows 11, x64 | Windows Server 2025 (x64) and Windows 11 on Arm (x64 emulation) | [CrumbWriter-3.1-Windows-x64-Setup.exe](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/CrumbWriter-3.1-Windows-x64-Setup.exe) |
| macOS 15 or later, Apple silicon and Intel | macOS 15 on both CPU architectures | [CrumbWriter-3.1-macOS-Universal.dmg](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/CrumbWriter-3.1-macOS-Universal.dmg) |

Crumb Writer uses Qt 6. On FreeBSD and Linux you can run it on a desktop such as GNOME or Xfce, under a standalone X11 window manager, or with a Wayland compositor.

Choose the FreeBSD package for your major version. Its Qt libraries must be available from your configured package repository. Each Ubuntu LTS package covers its release series, including point updates. Debian 13 has its own package built against Debian’s libraries. On other Ubuntu and Debian versions, compatibility depends on the available Qt, Hunspell, and GLib versions. You can also build from source on other Linux distributions that provide those development packages.

On Windows 11 on Arm, the installer runs the x64 build through Windows emulation. The Windows and Mac downloads include Qt, American English spelling, and the offline thesaurus. On FreeBSD, Ubuntu, and Debian, the package manager installs them.

### License and warranty

Crumb Writer’s original code is dedicated to the public domain. Anyone may use, copy, modify, sell, or redistribute it. The software is provided “as is,” without warranty. To the fullest extent permitted by applicable law, the authors disclaim liability for claims or damages arising from its use. The BSD first badge has a separate CC0 dedication, included with the app. You retain your rights to your writing. [Full text of the Unlicense](LICENSE)

The bundled libraries, spelling dictionary, and WordNet thesaurus data have their own licenses. The Windows installation folder and the Mac app’s `Contents/Resources` folder contain `third-party/README.txt` and the individual notices. Corresponding library sources and build recipes are available alongside the installers on each release page. Inside the editor, **About Crumb Writer → View License** opens the full Crumb Writer license offline.

### Install and update on FreeBSD

**First installation.** Download the FreeBSD package above for a 64-bit Intel or AMD computer running FreeBSD 15.x or 14.x. From the directory containing the download, run the following as root. The leading `./` tells `pkg` to install the file you downloaded. It obtains dependencies from your configured FreeBSD repositories.

~~~sh
pkg install ./crumbwriter-3.1-FreeBSD-15-amd64.pkg
~~~

On FreeBSD 14.x, use `crumbwriter-3.1-FreeBSD-14-amd64.pkg` in the install command.

Open the desktop application launcher, type **Crumb Writer**, and press **Enter**. Press **Ctrl+O** to open a document. The next time you start the editor, it restores your open windows and tabs. **Help → About Crumb Writer** shows the installed version.

**Update.** Save your writing and close the editor. Download the newer package from the latest release and repeat the install command with the new version in its filename. To uninstall, run `pkg delete crumbwriter`. Your documents and their companion files stay where you saved them.

### Install or update on OpenBSD

Download the OpenBSD package into its own folder and verify it with the release checksum or attestation. Open a terminal in that folder. As root, run the first command for a new installation or the second for an update. Only that folder is trusted for the independently distributed package. OpenBSD supplies the signed dependencies from its usual mirror.

~~~sh
env TRUSTED_PKG_PATH="$PWD" PKG_PATH=installpath pkg_add "$PWD/crumbwriter-3.1.tgz"
env TRUSTED_PKG_PATH="$PWD" PKG_PATH=installpath pkg_add -u "$PWD/crumbwriter-3.1.tgz"
~~~

Launch **Crumb Writer** from the application menu or run `crumbwriter` in a graphical session. To remove the package, run `pkg_delete crumbwriter` as root.

### Install and update on Ubuntu or Debian

**First installation.** Choose the package corresponding to the Ubuntu or Debian version installed on your 64-bit Intel or AMD computer. The example uses Ubuntu 24.04 LTS. In the directory containing the file, run the following commands. APT installs Crumb Writer and its dependencies, including the US English spelling and thesaurus data.

~~~sh
sudo apt update
sudo apt install ./crumbwriter_3.1-ubuntu24.04_amd64.deb
~~~

For Ubuntu 26.04, use `crumbwriter_3.1-ubuntu26.04_amd64.deb` in the command. On Debian 13, use `crumbwriter_3.1-debian13_amd64.deb`.

Open **Crumb Writer** from the desktop application menu. Use **Ctrl+O** to open a file and **F1** for the searchable keyboard guide.

**Update.** Close the editor, download the newer `.deb`, and run `sudo apt install ./` followed immediately by the new filename. Each new version comes from the GitHub release page. To uninstall the app, run `sudo apt remove crumbwriter`.

**Switching from a source build.** A previous Ubuntu `sudo make install` normally placed an executable in `/usr/local/bin`. If the launcher still opens that copy, check `command -v crumbwriter`. When it reports `/usr/local/bin/crumbwriter`, close the editor and rename that old executable with `sudo mv /usr/local/bin/crumbwriter /usr/local/bin/crumbwriter-source`. The packaged executable is `/usr/bin/crumbwriter`. A personal installation in `~/.local/bin` can take precedence the same way.

### Install or update on Windows

1. Download the Windows installer. For confirmation that the file came from this repository, [verify the download](#verify-a-download).
2. Open the installer. It installs for the current account and provides a Start menu entry. Setup also offers a desktop shortcut, a **View License** button, and the option to list Crumb Writer under **Open with** for text files while preserving the current default editor. Open **Crumb Writer** from Start.
3. To update, close the editor and run the newer installer. To uninstall, select **Crumb Writer** under **Settings → Apps → Installed apps** (**Apps & features** on Windows 10). Your documents, their companion files, and your personal settings stay in place.

The installer has no Windows publisher certificate. SmartScreen may display **Windows protected your PC** or an unknown publisher. After verifying the file, **More info → Run anyway** allows this installation where Windows policy offers that choice. [Microsoft’s guide to SmartScreen](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

### Install or update on macOS

1. Download the Mac disk image. The same file runs natively on Apple silicon and Intel Macs. [Verify the download](#verify-a-download).
2. Open the disk image in Finder and drag **Crumb Writer** to **Applications**. With the keyboard, select the app in the disk image, press **Command+C**, open Applications with **Command+Shift+A**, and press **Command+V**. Eject the disk image and open the installed copy.
3. The app carries an ad hoc signature and has no Apple notarization. On the first launch, Gatekeeper may ask for approval. After attempting to open the verified app, open **System Settings → Privacy & Security**, locate the notice for **Crumb Writer**, and choose **Open Anyway**. This grants an exception to this copy of the app. [Apple’s guide to opening downloaded apps](https://support.apple.com/en-us/102445)
4. To update, quit Crumb Writer and replace the application with the newer copy. To uninstall, move the application to Trash. Your documents and their companion files stay where you saved them.

### Verify a download

GitHub Actions creates a signed build record, called an **artifact attestation**, for every downloadable package and the checksum list. With GitHub CLI installed and signed in, run the command for the file just downloaded, in the folder containing it.

~~~text
gh attestation verify crumbwriter-3.1-FreeBSD-15-amd64.pkg --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter-3.1-FreeBSD-14-amd64.pkg --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter-3.1.tgz --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter_3.1-ubuntu26.04_amd64.deb --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter_3.1-debian13_amd64.deb --repo AlfredoLlaquet/crumbwriter
gh attestation verify crumbwriter_3.1-ubuntu24.04_amd64.deb --repo AlfredoLlaquet/crumbwriter
gh attestation verify CrumbWriter-3.1-Windows-x64-Setup.exe --repo AlfredoLlaquet/crumbwriter
gh attestation verify CrumbWriter-3.1-macOS-Universal.dmg --repo AlfredoLlaquet/crumbwriter
~~~

A successful check identifies the repository, build workflow, and source revision that produced the downloaded bytes. Windows SmartScreen and Apple Gatekeeper make their own decisions when opening the program. [GitHub CLI verification guide](https://cli.github.com/manual/gh_attestation_verify)

For a quick integrity check, download the checksum list for the same release and compare the 64 hexadecimal characters beside your filename with your local result. On FreeBSD, run `sha256 -q crumbwriter-3.1-FreeBSD-15-amd64.pkg`. On Ubuntu, run `sha256sum crumbwriter_3.1-ubuntu24.04_amd64.deb`. On macOS, run `shasum -a 256 CrumbWriter-3.1-macOS-Universal.dmg`. In Windows PowerShell, use `Get-FileHash .\CrumbWriter-3.1-Windows-x64-Setup.exe -Algorithm SHA256`. [Version 3.1 checksums](https://github.com/AlfredoLlaquet/crumbwriter/releases/download/v3.1/SHA256SUMS)

The repository uses GitHub’s **immutable releases**. When a release is published with this setting, GitHub locks the tag to its source commit and locks the attached files. The signed build record independently lets a reader verify the downloaded bytes. [GitHub’s immutable release guide](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)

## Build and run on FreeBSD

### First installation

For a source installation on FreeBSD 14.x or 15.x, install the dependencies as root.

~~~sh
pkg install git pkgconf glib qt6-base hunspell en-hunspell en-mythes poppler-utils
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

Open the desktop application launcher, type **Crumb Writer**, and press **Enter**. **Ctrl+O** opens a document with the cursor at the end. By default, the editor restores your open windows and tabs at startup. **F1** opens the searchable keyboard guide.

For a personal installation, run `make install PREFIX="$HOME/.local"` as your desktop account. That account’s graphical session needs `$HOME/.local/bin` on its `PATH` so the launcher can find the executable.

### Update

When updating a source build from 2.5.1 or earlier, install the thesaurus data as root with `pkg install en-mythes`. Save your writing and close Crumb Writer. Update the existing checkout as your desktop account.

~~~sh
cd crumbwriter
git pull --ff-only
~~~

In the updated directory, repeat the original installation command and prefix. For a system installation, run `make install` as root. Reopen **Crumb Writer** from the application menu. **Help → About Crumb Writer** opens About, which shows the installed version.

## Build and run on OpenBSD

The 8.0 amd64 package is built with the OpenBSD ports infrastructure. Install the base and X sets and use the **8.0 release** ports tree. The tree belongs in `/usr/ports`. With a fresh tree, run the following as root.

~~~sh
cd /tmp
ftp -o ports.tar.gz https://cdn.openbsd.org/pub/OpenBSD/8.0/ports.tar.gz
ftp -o SHA256.sig https://cdn.openbsd.org/pub/OpenBSD/8.0/SHA256.sig
signify -C -p /etc/signify/openbsd-80-base.pub -x SHA256.sig ports.tar.gz
tar -xzf ports.tar.gz -C /usr
PKG_PATH=https://cdn.openbsd.org/pub/OpenBSD/8.0/packages/amd64/ pkg_add git cmake-core ninja qt6-qtbase glib2 libarchive hunspell poppler-utils desktop-file-utils gtk4-update-icon-cache xz
~~~

The command uses the signed packages built for the release ports tree. On a machine already using updated stable libraries, use its matching OpenBSD 8.0 stable ports tree (CVS tag OPENBSD_8_0). Clone the matching Crumb Writer release as your desktop account. From the checkout, run the remaining commands as root to install the port under `mystuff`, compile the editor and install the resulting package. The port fetches the versioned source archive, checks its digest and installs the spelling and thesaurus data.

~~~sh
git clone --branch v3.1 https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
mkdir -p /usr/ports/mystuff/editors/crumbwriter
cp -R packaging/openbsd/port/. /usr/ports/mystuff/editors/crumbwriter/
cd /usr/ports/mystuff/editors/crumbwriter
make install
~~~

Run `crumbwriter --smoke-test` in your desktop session to check the installed editor, spelling, thesaurus and file handling. [OpenBSD ports guide](https://www.openbsd.org/faq/ports/ports.html)

## Build and run on Ubuntu

The release includes native Ubuntu 24.04 LTS, Ubuntu 26.04 LTS, and Debian 13 packages. From a terminal on the desktop account, install the build tools, Qt 6, GLib, Hunspell, and the American English spelling and thesaurus dictionaries, then build and install the desktop application.

~~~sh
sudo apt update
sudo apt install git build-essential pkgconf qt6-base-dev qt6-wayland libglib2.0-dev libhunspell-dev libarchive-dev poppler-utils hunspell-en-us mythes-en-us libxcb-cursor0
git clone https://github.com/AlfredoLlaquet/crumbwriter.git
cd crumbwriter
sudo make install
~~~

Open the desktop application menu, search for **Crumb Writer**, and press **Enter**. For a source update from 2.5.1 or earlier, first run `sudo apt install mythes-en-us`. For subsequent updates, close the application, run `git pull --ff-only` in the checkout, and run `sudo make install` again. On either platform, `make` as your desktop account can perform the compilation ahead of the system installation. Run `make check` in a graphical session for the parser, storage, and editor tests. [Unix package, desktop, and tiling checks](https://github.com/AlfredoLlaquet/crumbwriter/actions/workflows/check.yml)

## Import and export

**File → Import** opens a converted document in a new, editable, untitled tab. Use **Save As** to give that manuscript a name. The supported inputs are Word (DOCX), OpenDocument (ODT), Markdown, HTML, RTF, PDF with a text layer, and plain text. An image-only PDF needs OCR before import. Headings, paragraphs, lists and italics are converted where the source supplies them. Page artwork, complex page layouts and PDF reading order may need attention after conversion.

For plain text, select an encoding and check the preview. Choose **Fixed-width text** for a file hard-wrapped at 80 columns or enter its actual line width. The importer joins wrapped prose while preserving blank lines and new list items.

**File → Export** writes a separate copy. DOCX, ODT, HTML and PDF use the selected writing font, size, heading hierarchy and italics. The receiving device needs the selected font for editable formats. PDF export checks that the selected font allows embedding. For legacy text, choose a code page. If it cannot represent a character, the export stops before replacing the destination. **Fixed-width text** wraps at 80 columns and expands one space after a period to two. Ordinary **Save** keeps the native UTF-8 manuscript and companion files.

## Plain text file format

Crumb Writer saves documents in both modes as **UTF-8 without a byte order mark (BOM)**. UTF-8 keeps accents, curly quotes, and symbols such as ♫ in the text itself. If you open a UTF-8 file that begins with a BOM, the editor omits that mark the next time it writes the file. The startup backup preserves the original bytes, including any BOM.

One file line holds a paragraph in Generic mode or a piece in Crumb mode. The text wraps to the width of the writing area on screen. Resizing the window never inserts line breaks into the file. A saved nonempty document ends with one line ending. Empty lines between paragraphs are preserved, extra blank lines at the end are trimmed, and an empty document saves as an empty file.

The first line determines the mode. A `//` outside a web address switches the editor to Crumb mode. All other first lines, including a blank line or `https://www.thecrumbs.org/`, select Generic mode. To open an older collection that begins with `Crumb 1:` in Crumb mode, insert the collection title and `//` before that first header. The mode updates as you edit and appears at the right end of the status bar.

## Writing Crumbs in Crumb Mode

Each crumb has three parts on one line of the file. The first colon ends the **header**, and the second ends the **title**. Everything after the second colon is the **body**, which can contain more colons. The header consists of the collection’s **label**, a space, and the crumb’s **number**. Start at 1 so that crumb 200 occupies line 200. You can leave the title, the body, or both empty.

Start the first line with the title of your collection, immediately followed by `//` and the first crumb. Give the collection a distinctive label of its own. Here the collection is *Cranolos - Manolo’s Crumbs*, its label is `Cranolo`, and its first piece is *The Inspector*.

~~~text
Cranolos - Manolo’s Crumbs//Cranolo 1: The Inspector: A goose audits the moon for bumps.
Cranolo 2: Night Audit #1: The tide submits its receipts.
Cranolo 3: Night Audit #2: One receipt smells of salt.
~~~

The third piece is both **Cranolo 3** and **Night Audit #2**. Its header number gives its place in the collection. Its genre number counts only the pieces in the *Night Audit* series. Type `Night Audit #` in a later title to continue from the nearest earlier occurrence. To start the next piece in the series at the end of the file, press **Ctrl+Shift+4**, type part of the genre name, and press **Enter**. The editor fills in the header and title and puts the cursor at the start of the body. **Ctrl+N** starts a crumb with an empty title. If a header number differs from its line number or a genre number falls out of sequence, the digits appear in bold red. **F4** takes you to the next numbering warning.

On screen, the header is italic, the title is bold, and the body uses your chosen writing font. **Tab** moves between titles and bodies. **Ctrl+Down** moves to the next crumb. **Ctrl+Q** selects and copies the current piece, leaving out the collection title on line 1. Each new piece appears immediately in **Visit history**. Its entry updates as you type the title. Two pieces with the same title still have separate entries.

To start your own collection in a blank document, type its title followed immediately by `//`. Press **Ctrl+N** to create the first header on that same line. Replace the default label, `Crumb`, with your own. The editor reuses it for the next piece.

See how line 1 of Alfredo Llaquet-Alsina’s `Crumbs.txt` becomes *The Game* in the official Crumb Reader. The site reads the same plain text file, checks the numbers against the lines, and displays each piece with its title and body. A reader can open a link to that piece, mark it as a favorite, or search the rest of the collection. [Source text, line 1](https://github.com/AlfredoLlaquet/Alfredo_Llaquet-Alsina_Crumbs/blob/main/Crumbs.txt#L1) · [Reader code](https://github.com/AlfredoLlaquet/Alfredo_Llaquet-Alsina_Crumbs/blob/main/Reader/core.js) · [Read *The Game*](https://www.thecrumbs.org/Reader/?crumb=1)

## Writing Novels and Other Prose in Generic Mode

Press **Enter** when you finish a paragraph. Put the next paragraph on the next line. A long paragraph wraps as you write and reflows when you resize the window. A blank line can mark a pause in an essay or a scene break in a novel. The same plain text file can take a book from its first draft through editing to publication.

Here are the first six lines of `Harbor.txt`.

~~~text
The Harbor of Borrowed Tides

The Night Clerk
---------------
At dusk the harbor clerk signed for a tide that had arrived in six wooden crates.
By dawn the water had learned the shape of the pier.
~~~

**Giving the document a title.** The editor shows the first line in its largest bold type if that line contains text and meets any one of four conditions. It has no period, its letters are all uppercase, a blank line follows it, or a heading marker follows it. A marker lets you use a title such as *Mrs. Dalloway* and start your first paragraph directly below the marker. If you leave a blank line directly under a general title, it appears as a narrow gap.

**Organizing a book.** Write the heading on its own line. On the following line, type one of these characters at the left edge.

| Marker | Level | An example use |
| --- | --- | --- |
| `=` | 1 | Part |
| `-` | 2 | Chapter |
| `~` | 3 | Section |
| `.` | 4 | Subsection |

One character is enough. When you leave the marker line, the editor repeats that character to match the visible length of the heading, including spaces. *The Night Clerk* takes 15 hyphens. Accents and emoji count as the characters you see. While you’re on the line, you can still type a hyphen, a space, and `Buy coffee` to make a list item, even if the editor saves in the meantime. A marker contains only repetitions of its own character. A period at the end of a sentence stays part of the sentence.

Use the toolbar’s **Style** list or **Format → Style** to change the current line. On line one, choose **Main Title** or **Regular Text**. On later lines, choose **Regular Text** or **Title 1** through **Title 4**. Press **Ctrl+Shift+K**, release it, then press **M**, **0**, or **1–4**, respectively. Choosing Regular Text on the first line also turns off automatic main-title detection for this document.

In the normal writing view, headings use size, weight and spacing. **View → Show manuscript markup** (**Ctrl+Shift+F9**) reveals the marker rows, italic delimiters and the on-screen heading underlines. Once you leave a marker row, its repeated characters span the visible width of the heading, including in a proportional font. Link and spelling underlines keep their usual meanings. Each empty line in the body leaves one line of space. A paragraph surrounded by blank lines stays ordinary prose. A blank line you just opened at the end stays available for typing through an autosave.

**Writing in italics.** Enclose a passage in guillemets, the angled quotation marks in `«the other moon»`. The words appear in italics. You can see the marks while the cursor is in the passage, and they disappear from view when you move away. **Ctrl+K** inserts the opening or closing mark. With text selected, it adds or removes the pair in one undo step. Each pair belongs to one paragraph. If you use guillemets for dialogue, open **Manuscript Settings** and select **Display guillemets literally**. The same dialog offers **Disable general title detection**, which lets the first paragraph remain ordinary prose unless you give it a heading marker. Both choices travel with the document in `Harbor.txt.config`.

**Writing lists.** Start a new paragraph after `1. Buy coffee` and it begins with `2. `. Bullets, lettered lists, Roman numerals, and multilevel lists such as `1.9.` → `1.10.` continue the same way. Press **Enter** on an empty item to end the list, or **Backspace** immediately after creating it to remove the suggested prefix. You can edit any prefix. An ordinary sentence beginning with *I* stays prose. Use **Continue list** (**Ctrl+Shift+Enter**) when you mean to start a list and the first item could be read either way. If you type the proposed marker yourself or paste an item that already begins with a marker, the line gets one prefix. Pasting an explicitly numbered item keeps its supplied number.

**Finding your place.** **Ctrl+Up** and **Ctrl+Down** move between file lines. **Tab** and **Shift+Tab** jump between headings. **Ctrl+Shift+4** focuses **Titles**. Type part of a title and press **Enter** to go there. **Shift+F7** switches between document order and alphabetical order. Document order shows the headings as a tree. Alphabetical order shows a flat index with parent headings for context. The outline follows the section you’re writing. Choosing the general title brings it to the top of the writing area. **End of document** is the last entry in either order and takes you to the end of the last paragraph. **Ctrl+Shift+3** searches the full text of visited paragraphs, which appear under their opening words. The right panel shows the day’s net word count. Deleting 150 more words than you add shows −150.

**Using the file outside the editor.** To find the passage about the crates, run this command in the folder containing the book.

~~~sh
grep -nF 'six wooden crates' Harbor.txt
~~~

The result starts with `5:At dusk` and includes the full paragraph. Compare two drafts line by line and you can see which paragraph changed. A script can also recognize a heading by looking one line ahead for its `=`, `-`, `~`, or `.` marker. All of those cues are readable at a glance when you open the file in another editor.

The same book can become a print edition, a website, or an ebook that reflows on a reader’s screen. A publishing script can turn the book title into an HTML `<h1>`, *The Night Clerk* into an `<h2>`, and the two prose lines into separate `<p>` elements. Each edition can have its own margins, typefaces, and page sizes. Correct the sentence about the crates once in `Harbor.txt` and the next edition can pick up that change. Years later, you can still read the file in any text editor.

For the rules the editor uses to recognize a document’s mode and save its text, see the [plain text file format](#plain-text-file-format).

## How Words Are Counted

Start at the crumb’s header and count through the end of its body. Each group of characters between spaces counts as one word. Tabs and other whitespace also separate words, as does an em dash. A slash with an ordinary space on each side separates words and contributes nothing to the count. Hyphens and apostrophes stay with their words, so `moon-lit / sand—bank` counts as three. Punctuation standing on its own counts as a word, too. That’s why `Crumb 2: :` has three words.

When you’re on line 1, the word count in the side panel leaves out the collection title. The **word count for the whole document includes it**. In the Cranolos example, the first crumb has 11 words. Its full line has 15 because `Cranolos - Manolo’s Crumbs` adds four, including the hyphen with a space on each side. To get the average, the editor divides the document’s total word count by the number of recognized crumbs.

To reproduce the count yourself on FreeBSD, Linux, or macOS, run the command below. On line 1, `sed` replaces the first literal `//` with a space. On every line, it replaces each em dash and each nonoverlapping ` / ` with a space. Then `wc -w` counts the words. For the first crumb alone, remove the collection title and its `//` separator before applying the last two substitutions to that line.

~~~sh
sed -e '1s@//@ @' -e 's/—/ /g' -e 's@ / @ @g' Crumbs.txt | wc -w
~~~

The exact count for unusual Unicode characters depends on the platform’s `wc -w`. FreeBSD and macOS check the remaining UTF-8 bytes individually. On Ubuntu, GNU `wc` reads whole characters in a UTF-8 locale. The Windows build reproduces FreeBSD 15.x’s `C.UTF-8` result, independent of the Windows display language. [FreeBSD implementation](https://github.com/freebsd/freebsd-src/blob/releng/15.0/usr.bin/wc/wc.c) · [Apple implementation](https://github.com/apple-oss-distributions/text_cmds/blob/main/wc/wc.c) · [GNU word counting](https://www.gnu.org/software/coreutils/manual/html_node/wc-invocation.html)

Generic mode uses the same rules for spaces, em dashes, and slashes surrounded by spaces. A web address keeps its slashes. **Manuscript Statistics** counts headings as nonblank paragraphs. It leaves blank lines and heading markers out of the paragraph count, word count, and average.

<a name="working-with-several-manuscripts"></a>

## Working with several documents

**Ctrl+O** opens a document in a tab. If it’s already open, you return to its tab, even in another window. Switch tabs with **Ctrl+Tab** and **Ctrl+Shift+Tab**. The panels follow the active document. Each tab keeps its own undo history, search, bookmarks, dictionary, selection, and scroll position. The **+** beside the tabs, a double-click on empty tab-bar space, or **Ctrl+T** opens a blank tab, and **Ctrl+Shift+N** opens another window. To put a document on a second monitor, use **Window → Move tab to new window**. Its editing history goes with it.

By default the next launch restores the windows, tab order, selected tab, caret and scroll positions, including retained untitled drafts. **Settings → Restore open windows and tabs at startup** controls this behavior. A missing file is reported while the remaining tabs reopen. **Ctrl+F4** closes a tab. **Ctrl+Shift+T** reopens the most recently closed saved document where you left it. **Alt+W** closes the window. If any documents still have changes waiting to be saved, the editor identifies them before closing. In the writing area, **Ctrl+W** deletes a word and **Ctrl+N** appends a crumb or paragraph.

## Writing and navigation

When inserting between two words, you can temporarily leave extra spaces around the caret. The editor tidies the gap once you finish editing it. In `first—second`, **Ctrl+Right** and **Ctrl+Left** stop on each side of the em dash.

On a Mac, **Ctrl** means the physical **Control** key and **Alt** means **Option**. The usual **Command** shortcuts also work for New Window, Open, Save, Save As, Find, Select All, Cut, Copy, Paste, Undo, and Redo. **Command+,** opens Settings, **Command+W** closes the current window, and **Command+Q** quits the editor through its normal save confirmation. **Control+Shift+Space** starts movement selection when **Control+Space** switches input sources. **Control+Shift+B** opens Bookmarks, and **Control+Shift+W** toggles Wrap in Find, leaving Option available for typing special characters. On keyboards whose top row controls media, hold **Fn** for function keys such as **F1**.

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Append a crumb and start its body, or start a new paragraph at the end in Generic mode |
| Ctrl+1 | In Crumb mode, select the header and copy it in lowercase, such as `crumb 42` |
| Ctrl+2 | Select the word at the cursor, including internal apostrophes and hyphens |
| F7 | Look up the selected text or the word at the cursor in the offline English thesaurus |
| Ctrl+Shift+L, then D / S / T | Open the Merriam-Webster dictionary entry / its synonyms section / the thesaurus entry |
| Ctrl+Q | Select and copy the current crumb or paragraph. The selection leaves out its ending line break and, for crumb 1, the collection title |
| Ctrl+Y | Delete the current crumb or paragraph while writing |
| Ctrl+W | Delete the selection, word, or space at the cursor |
| Escape | Clear the text selection and close Find or Replace when open |
| Tab / Shift+Tab | Move between crumb titles and bodies, or between headings in Generic mode |
| Ctrl+Up / Ctrl+Down | Previous / next crumb or paragraph |
| Up / Down | Previous / next wrapped line, including virtual rows below the file |
| Home / End | Start / end of the wrapped line |
| Ctrl+Home / Ctrl+End | Start / end of the current crumb or paragraph |
| Ctrl+Page Up / Ctrl+Page Down | Start of file / end of last crumb or paragraph |
| Ctrl+Shift with Up, Down, Home, End, Page Up, or Page Down | Select to the corresponding line or file destination. Keep Shift held to extend or shrink from the same starting point |
| Ctrl+Space | Preview a movement selection from the cursor. Enter accepts it |
| Ctrl+Enter / Alt+Enter | Center the current visual line / place the current crumb or paragraph at the top |
| Ctrl+L | Go to a crumb by its header number or, in Generic mode, to a line by its number in the file |
| Ctrl+P / Ctrl+R | Start a plain text or regular expression search from the cursor |
| Ctrl+U | Open Insert Symbol with the search field focused |
| F5 in the writing area | Accept a suggested name ending or open a list of completions |
| F6 / Shift+F6 | Previous / next mention of a character, recurring term, or crumb genre |
| Ctrl+Shift+J / Ctrl+click | Open the web link, follow the numbered crumb reference, or find the previous mention of the name at that position |
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
| Ctrl+Shift+D / Ctrl+Shift+I | Open Manuscript Dictionary / Manuscript Statistics for the current document |
| Ctrl+B / Alt+B | Add a bookmark / open the separate Bookmarks window |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo a run of typing, Backspaces, or forward Deletes |
| Ctrl+Shift+N / Alt+W | Open a new window / close the current window from the writing area |
| Ctrl+, / F10 / F1 | Settings / File menu / searchable keyboard reference |
| Ctrl+. | Choose a spelling correction for the word at the caret |

Press **Ctrl+,** and choose **Appearance** to follow the desktop or select **Light** or **Dark**. The choice applies to the toolbar, search fields, lists, menus, and editor dialogs. The menu bar groups commands under **File**, **Edit**, **View**, **Format**, **Navigate**, **Tools**, **Window** and **Help**. **F10** opens File. Beside **Find**, **Font** selects an installed family and the adjacent number sets its size. A new installation uses 14-point Georgia on Windows, Charter on macOS and Noto Serif on BSD and Linux, with a system serif fallback. Existing font preferences take precedence. **Help → Crumb Writer Help** opens this guide offline. The three-line buttons beside **Characters** and **Recurring** open their list actions. A focused menu button opens with **Space**, **Enter**, or **Down**. **Escape** closes the menu.

A faint tint of your desktop accent color follows the current crumb or paragraph across all its wrapped lines. When another window becomes active, the writing area turns a softer gray. You can use the panels or search field and keep the active writing colors. Change the writing font and size in **Settings**. The middle of the status bar links to **Read Alfredo Llaquet-Alsina’s Crumbs at TheCrumbs.org**. In a narrow window or during a movement command, the writing status takes that space. You can also reach the site through **Help → Open TheCrumbs.org**.

Smart punctuation writes curly quotes and apostrophes, em dashes, and ellipses into the file as you type. Pasting turns tabs and repeated spaces into single spaces and trims trailing spaces in one undo step. When you backspace over the last word of a line, the space before it stays at the cursor so you can type a replacement, even through an autosave. The saved file omits trailing spaces.

Consecutive typing forms one undo step, including spaces, pauses, automatic punctuation, and live saves. Repeated presses of **Backspace** or of **Delete** each form one step as well. Moving the cursor, pressing **Enter**, pasting, or switching between typing and deletion starts another step.

**Settings → Writing → Copy deleted text to the clipboard** starts off. Turn it on to copy a selection you delete, a word you remove with **Ctrl+W**, or a whole crumb or paragraph you remove with **Ctrl+Y**. Backspacing through `goose` one character at a time keeps your previous clipboard entry. Holding down **Delete** does the same. **Cut** always copies. With deletion copying enabled, pasting over a selection puts the displaced passage on the clipboard, and Replace All collects the passages it replaces in document order. Your choice carries over when you upgrade.

Undo immediately after **Ctrl+N** returns the cursor, selection, and view to where they were before you added the new crumb or paragraph. Redo takes you back to the new writing position.

**Visit history** shows crumb titles or the opening words of paragraphs. An empty crumb title appears as *Untitled*. Choose an entry to return to the cursor position you last used there.

### Insert a Unicode symbol

Press **Ctrl+U** to open **Insert Symbol**. Search for `outline heart`, `music`, a symbol itself, or a code point such as `U+2661`. The grid contains 733 symbols in 17 themes, including typography, mathematics, arrows, and music. The preview shows the symbol in your writing font, along with its name and code point.

**Alt+S** or **Ctrl+F** focuses Search. **Alt+T** or **Ctrl+T** opens the theme selector. Press **Down** to enter the grid and browse with the arrows. **Enter** or a double-click inserts the symbol and closes the window. **Ctrl+Enter** inserts it and keeps the window open. **Ctrl+D** toggles a favorite, and **Escape** closes the window. Your favorites and recent symbols are available in every document on the same computer. Each symbol is ordinary text you can undo in one step. [Catalog and Unicode data notice](data/symbols.tsv)

### Arrange panels and columns

Use **View → Arrange Panels and Columns** or **Ctrl+Shift+F10** in a window with adjustable panels or columns. Select one, press **Space** to show or hide it, and use the move and size controls to adjust it. The editor remembers the arrangement for each kind of window on that computer. It also remembers the size of a floating window. If you use Krohnkite to tile two editor windows, each fits the space the window manager gives it.

**View → Restore Default Layouts** returns panels and columns to their original arrangements in all windows. Your writing preferences are stored separately.

### Live movement selections

Press **Ctrl+Space** in the writing area. Type a movement, add an optional count **after** it, and press **Enter** to keep the selection. The status bar shows the command. Each keystroke previews the selection from your original cursor position. For example, `j` selects into the next file line. Add `2` to select into the second line, or press **Backspace** to go back to one. **Escape** restores the original cursor and selection. You can correct an incomplete command or change a search that found nothing before pressing **Enter**.

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

**Ctrl+F** searches forward from the cursor. **Ctrl+G** searches backward from the same starting point. In a plain text search, typing immediately selects and centers the nearest match in that direction. As you type more letters, the search still starts where you first placed the cursor. **F3** repeats the search in the original direction. **Shift+F3** reverses it for one match.

**Ctrl+H** opens Find and Replace. Press it again to switch between **Find** and **Replace with**. You can replace plain text or text matched by a regular expression. **All…** previews the affected lines with before-and-after examples. **Apply all** makes the changes, and a single Undo reverses them.

| Find and Replace controls | Action |
| --- | --- |
| Ctrl+P / Ctrl+R / Ctrl+L | Plain text / regular expression / number navigation for the current mode |
| Ctrl+M / Ctrl+W / Alt+W | Toggle match case / whole words / wrap |
| Alt+Down | Open and focus recent searches |
| Enter / Shift+Enter in the query | Continue in the opening / opposite direction |
| Escape, including from the writing area | Close the search bar and clear the text selection |

Search starts case-insensitive, with wrap enabled. Regular expressions prefer the shortest match by default. For example, `“.*”` finds one quoted passage on a line, and `(?-U)“.*”` allows a match through the last closing quote. Recent queries include their search options. [PCRE2 syntax](https://www.pcre.org/current/doc/html/pcre2syntax.html) · [Pattern guide](https://www.pcre.org/current/doc/html/pcre2pattern.html)

## Characters, Recurring, and spelling

**View → Characters** shows or hides the Characters list. Its initial state is hidden in Generic mode and shown in Crumb mode. Each mode remembers its own choice. Right-click a sidebar entry to act on that entry. Character and recurring entries offer editing, deletion, finding all occurrences and insertion at the writing caret. Titles, genres and visit history offer the operations that apply to those entries.

Use **Ctrl+Shift+1** for **Search Characters** or **Ctrl+Shift+2** for **Search Recurring**. Type part of any name in an entry. The first match is highlighted, and all entries stay visible. **Up** and **Down** move between matches. From the search field or the list, **Enter** finds passages containing any of the entry’s names. **Ctrl+Enter** inserts its first name where you were writing. Either action clears the search field. **Escape** returns you to the writing area.

Press **Insert** to add an entry. If you’ve selected text in the document, it appears in the new entry, selected so you can edit or replace it. Otherwise the field opens blank. **Enter** saves the entry, **F2** edits an existing one, and **Delete** removes it after confirmation.

On a compact Mac keyboard, **Control+I** opens Add from either the Characters or Recurring search field or list. **Backspace** (labeled **Delete** on many Macs) removes the selected entry from a list of names, bookmarks, or dictionary words. In a text field it deletes the preceding character.

The right panel works the same way. **Ctrl+Shift+3** searches **Visit history**, and **Ctrl+Shift+4** searches **Genres** or **Titles**. Choose a visit or title to go to its passage. In **Genres**, press **Enter** to write the next crumb in the highlighted genre, or **Ctrl+Enter** to see every mention of that genre in the document.

In a sidebar, Bookmarks, or Insert Symbol, a filled search field shows a **Clear Search** button. From the keyboard, select all (**Ctrl+A**, or **Command+A** on a Mac) and press **Backspace** to clear the field and keep focus there.

Each group of names occupies one line in the document’s `.characters` or `.recurring` companion file. The first name sets the alphabetical order. For example, `"Fox, Captain", Red Fox, Captain Fox` gives one character three names. Use double quotes around a name that contains a comma. In Add or Edit, **Exclude from autocomplete** turns off automatic and on-demand **F5** suggestions for that entry. You can still find, underline, and insert its names, and spell check continues to accept them. That preference travels with the list.

When just one name fits what you’ve typed, the suggested ending appears in the writing area. By default it appears after three characters, at 30% of the normal text color against the paragraph background. For example, type `Inspector Goo` and press **F5** to complete `Inspector Goose`. When several names fit, **F5** shows a list. Choose one with the arrows and **Enter**, or dismiss the list with **Escape**. **Settings → Names** controls completion for each list independently (**Automatic**, **On demand**, or **Off**). You can change the minimum number of characters and the underline color. Each list also has its own underline setting (**All mentions**, **Current group only**, or **Off**).

Put the cursor on or immediately after a name. **F6** finds the previous mention under any name in the same entry. **Shift+F6** finds the next. When a name belongs to several entries, you can choose which one to follow. **Ctrl+Shift+J** or **Ctrl+click** follows a numbered reference such as `Cranolo 200` in Crumb mode, or goes to the previous mention of a name in either mode. **Ctrl+Shift+click** goes forward. On a Mac, **Command+click** and **Command+Shift+click** also work. Jumps stop at the first or last mention. **Alt+Left** or **F2** returns you to where you were writing, even if you clicked a word several lines away. **Alt+Right** or **Shift+F2** moves forward again.

Names match regardless of capitalization or the use of curly or straight apostrophes. A longer name covers a shorter name inside the same mention. If you’ve listed both `Inspector Goose` and `Goose`, “Inspector Goose saluted a goose” has two matches. The results, underlines, and navigation all follow this rule. Each result shows the matched text in bold in its surrounding passage. In Crumb mode you can follow or find a genre such as *Night Audit* anywhere it appears. Genres use the underline setting for **Recurring** and stay out of autocomplete.

American English spell check uses the Hunspell en_US dictionary and is on by default. Press **Ctrl+.** on a misspelled word, select a suggestion with the arrow keys and press **Enter** to replace that occurrence, or **Escape** to leave it alone. In Generic mode it checks all ordinary prose, even a line that looks like a numbered crumb header. Press **F8** to review a possible mistake. Choose a suggestion, replace one or all occurrences, ignore the word once or throughout the document, or add it to the dictionary. **Ctrl+Shift+D** opens **Manuscript Dictionary** for the current document. Its `.dictionary` companion file holds one word per line, and capitalization doesn’t affect matching.

Spell check accepts complete names in **Characters** and **Recurring**, as well as complete genre names in Crumb mode. To recognize a shorter name on its own, add it as another name in the entry. Straight and curly apostrophes both work in contractions such as `I’m`. Adding `Hedberg` also accepts `Hedberg’s` and `Hedberg's`. Web addresses are skipped. Use **Ctrl+,** to open Settings and switch spell check on or off.

### Links, word selection, and dictionaries

Web addresses appear underlined. **Ctrl+click** opens a link in your browser (**Command+click** also works on a Mac). With the cursor on the address, press **Ctrl+Shift+J**, or choose **Open Link** from the writing area’s context menu. An address starting with `www.` opens using HTTPS. A sentence-ending comma or period stays in the prose. A closing parenthesis that belongs to the address stays with the link.

Double-click or press **Ctrl+2** to select a word. In `notwithstanding—not`, either word can be selected separately, and clicking the em dash selects the dash itself. Internal punctuation stays with words such as `O’Brien` and `well-read`.

Press **F7** to look up the selection or the word at the cursor in the offline English thesaurus. Scroll through the alternatives by meaning, choose one with the arrow keys, and press **Enter** to replace the original wording in one undo step. **Ctrl+C** copies the alternative, **Ctrl+L** looks up another word, and **Escape** closes the window. For a word such as `doctor`, choose the meaning you intend before picking an alternative.

For Merriam-Webster in your browser, select a phrase or put the cursor on a word. Press **Ctrl+Shift+L**, release the keys, then press **D** for the dictionary, **S** for the entry’s synonyms section, or **T** for the thesaurus. **Escape** cancels the lookup. You can also choose the destination under **Tools → Look Up in Merriam-Webster**. Only these web lookups send the word or phrase to the website.

## Statistics and daily count

**Ctrl+Shift+I** opens **Manuscript Statistics**. It shows the number of crumbs or nonblank paragraphs, the total word count, and the average words per crumb or paragraph to two decimal places. **Ctrl+C** copies all three figures with their labels and your usual thousands separators.

In Crumb mode the daily counter tracks how many of today’s new crumbs remain. In Generic mode it shows the net change in words. Crumb Writer remembers each document’s counts on this computer between sessions. A new local date starts a new count. To set a starting figure, open **Tools → Set Today’s Count**, enter the value, and press **Enter**. **Reset to Zero** fills in zero for you to confirm. Generic mode accepts negative numbers. Further edits, including Undo, count from the figure you set.

## Bookmarks and saves

**Ctrl+B** bookmarks the current line. Choose a category and give the bookmark a name, or press **Enter** to use the next number in the last category. **Alt+B** opens your bookmarks as a searchable tree or list. Each one shows its line number and crumb title or the start of its paragraph. A category with one bookmark opens that bookmark directly. Insert a paragraph above a bookmarked line 200 and the bookmark moves to 201. Delete the bookmarked line and it points to the following line. At the end of the file it moves to the last remaining line, or line 1 in an empty document. Undo and Redo move bookmarks with the edit.

For `Harbor.txt`, bookmarks live in `Harbor.txt.bookmarks` in the same folder. Older bookmarks open at their recorded line numbers, up to the end of the document. Newer bookmarks also record clues from the surrounding text, so the editor can find a passage even after you edit it in another program. If the passage and its neighbors have disappeared, the editor uses the nearest valid line to the saved number. **Save As** carries the bookmarks, names, dictionary, and document settings to the new filename.

**Compare with Startup Backup** opens a read-only view of the paragraphs that have changed since this file was opened. The backup appears on the left and your current text on the right. Use **F3** and **Shift+F3** to move through changes, **F5** to compare again after editing, or **Escape** to close the comparison.

By default, Crumb Writer makes a dated backup beside the document before you start editing and saves in the background while you write. When closing with unsaved changes, choose **Save and Close**, **Discard Unsaved Changes**, or **Keep Editing**. With session restoration enabled, **Save and Restore Tabs** also keeps untitled work for your next launch. **Save and Close** waits until your latest edit reaches disk. If saving fails, the window stays open.

[CrumbWriter.txt](CrumbWriter.txt) documents the file format, counting rules, keyboard controls, backup and recovery behavior, code structure, and maintenance checks.

## Licenses and credits

**Help → Licenses** opens the project license, artwork and data notices, and the component inventory offline. The OpenBSD package includes original notices for its bundled word data. Windows and macOS also carry the notices for the libraries and tools they distribute. Their matching source archives and build recipes accompany the installers. [Third-party notices](NOTICE.md) · [Release downloads](https://github.com/AlfredoLlaquet/crumbwriter/releases)

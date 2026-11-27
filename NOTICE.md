# Crumb Writer credits and third-party software

Created and directed by **Alfredo Llaquet-Alsina**. Implemented by **ChatGPT**. The Unlicense covers the original Crumb Writer code. Writers retain their rights to the documents they create.

| Component | Use, origin, and redistribution terms |
| --- | --- |
| Qt 6 | Widgets, text rendering, format conversion, networking and platform plugins. The Windows and macOS packages dynamically link the applicable LGPL Qt modules. Users may replace these libraries and debug their modifications. The source release includes the build, installation and packaging recipes. |
| GLib and GIO | File handling, character-set conversion and platform integration. Preserve the upstream LGPL notices and corresponding source for the shipped version. |
| Hunspell | Spelling engine. Preserve the original Hunspell license choices and notices for the shipped distribution. |
| SCOWL and its contributors | The en_US spelling dictionary has terms and copyrights separate from the engine. The bundled data includes the complete original README_en_US.txt. |
| MyThes and Princeton WordNet | The vendored MyThes reader has its own BSD-style notice. The thesaurus data has the Princeton WordNet notice and readme. Preserve both with the corresponding data. |
| libarchive | Reads and writes ZIP containers for Office formats. Preserve the original copyright and component-specific terms for each bundled distribution. |
| Poppler | On BSD, Linux and Windows the external pdftotext program extracts PDF text. Its GPL license and its dependencies' own terms apply. The Windows package includes Poppler’s encoding maps with their upstream Adobe and GPL notices. Windows distributes the separate executable with its runtime, original notices, source and build recipes. The macOS edition uses Apple's system PDFKit. |
| Inno Setup | Creates the Windows installer. The installer includes the upstream Inno Setup license and copyright notice. |
| Unicode Consortium | Character names and code points come from UnicodeData.txt 15.0.0. The symbol catalog and the offline license viewer include the Unicode copyright and permission notice. |
| BSD first | The badge by the BSD first project is dedicated under CC0. The light and dark PNGs render the official SVG on a transparent background. |

The native installers carry their version-specific inventory and original notices in the third-party directory. Matching source archives, redistribution patches, recipes and checksums accompany the installers on the release page. Shared libraries can be replaced when rebuilding or repackaging the application. BSD and Linux resolve runtime dependencies through their package managers. System fonts are selected by family. DOCX, ODT and HTML reference that family. PDF export checks the selected font's embedding permissions.

[Source and releases](https://github.com/AlfredoLlaquet/crumbwriter/releases) · [Qt](https://www.qt.io/licensing/open-source-lgpl-obligations) · [GLib](https://gitlab.gnome.org/GNOME/glib) · [Hunspell](https://github.com/hunspell/hunspell) · [SCOWL](https://github.com/en-wl/wordlist) · [MyThes and WordNet](https://github.com/hunspell/mythes) · [libarchive](https://www.libarchive.org/) · [Poppler](https://poppler.freedesktop.org/) · [BSD first](https://github.com/AlfredoLlaquet/bsd-first)

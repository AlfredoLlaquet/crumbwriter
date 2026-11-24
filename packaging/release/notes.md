## Downloads

- [FreeBSD 15 package for amd64](https://github.com/@REPOSITORY@/releases/download/@TAG@/CrumbWriter-FreeBSD-15-amd64.pkg)
- [Ubuntu 24.04 LTS package for amd64](https://github.com/@REPOSITORY@/releases/download/@TAG@/CrumbWriter-Ubuntu-24.04-amd64.deb)
- [Windows x64 installer](https://github.com/@REPOSITORY@/releases/download/@TAG@/CrumbWriter-Windows-x64-Setup.exe)
- [macOS disk image for Apple silicon and Intel](https://github.com/@REPOSITORY@/releases/download/@TAG@/CrumbWriter-macOS-Universal.dmg)
- [Installation, platform requirements, and first-launch instructions](https://github.com/@REPOSITORY@/blob/@TAG@/README.md)

American English spelling and thesaurus lookups work offline. On Ubuntu and FreeBSD, the package manager installs the required dictionaries. The Windows and macOS downloads include them. Companion source archives contain the third-party libraries and dictionaries distributed with the Windows and macOS installers. Source archives ending in numbered parts can be joined in numeric order before extraction.

## Verify a download

GitHub signs a build record for the exact installer. With the GitHub CLI, run the command for the downloaded file.

```sh
gh attestation verify CrumbWriter-FreeBSD-15-amd64.pkg --repo @REPOSITORY@
gh attestation verify CrumbWriter-Ubuntu-24.04-amd64.deb --repo @REPOSITORY@
gh attestation verify CrumbWriter-Windows-x64-Setup.exe --repo @REPOSITORY@
gh attestation verify CrumbWriter-macOS-Universal.dmg --repo @REPOSITORY@
```

The assets include the signed build record and a SHA-256 checksum list. Windows may show a SmartScreen notice. The Mac app has an ad-hoc signature and macOS may ask for approval on first launch.

## License and warranty

Crumb Writer’s original code is dedicated to the public domain. Anyone may use, copy, modify, sell, or redistribute it. The software is provided “as is,” without warranty. To the fullest extent permitted by applicable law, the authors disclaim liability for claims or damages arising from its use. This dedication applies only to the editor. Writers retain their rights to their manuscripts. [Full license](https://github.com/@REPOSITORY@/blob/@TAG@/LICENSE)

Programmed by ChatGPT for Alfredo Llaquet-Alsina.

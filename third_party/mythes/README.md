# MyThes

The unmodified MyThes engine in this directory comes from commit
`dd077939ef2cf3a1a866e3d40ae7c6d5cc78abbb` of
[hunspell/mythes](https://github.com/hunspell/mythes/tree/dd077939ef2cf3a1a866e3d40ae7c6d5cc78abbb).
Its redistribution terms are in `COPYING`. The editor builds these two small
source files on every platform. It reads the installed US English MyThes
dictionary on FreeBSD and Linux. Windows and macOS packages include the
upstream WordNet dictionary, index and notices staged by
`packaging/shared/thesaurus.py`.

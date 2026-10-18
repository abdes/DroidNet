# Unicode data

The Unicode Character Database files the virtual path rules
([virtual paths](../Docs/virtual_paths.md)) are stated against, pinned to the
version that names the folder. Every implementation of the rules generates its
tables from these files: the engine through
[UnicodeCodeGen](../Tools/UnicodeCodeGen/README.md), the editor through its own
build.

- `DerivedAge.txt`: the assigned code points.
- `CaseFolding.txt`: the simple case folding (status C and S).

The files are © Unicode, Inc., under the Unicode License v3 (`LICENSE.txt` in
each version folder).

# UnicodeCodeGen

Generates the Unicode tables of the virtual path rules
([VirtualPath.h](../../VirtualPath.h)) from the pinned Unicode Character
Database in `ucd/<version>`:

- the assigned code points (`DerivedAge.txt`): a segment refuses any other;
- the simple case folding (`CaseFolding.txt`, status C and S): two paths that
  differ only by it are case-only siblings.

The Content build runs the generator when the data or the tool changes and
writes `Internal/Generated.UnicodeTables.h` into the source tree, where it is
versioned. The managed implementation of the rules uses the same data.

```powershell
python -m unicode_codegen.cli --ucd ucd/15.1.0 --cpp-out ../../Internal/Generated.UnicodeTables.h
```

The data files are © Unicode, Inc., under the Unicode License v3
(`ucd/<version>/LICENSE.txt`).

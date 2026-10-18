# UnicodeCodeGen

Generates the Unicode tables of the virtual path rules
([virtual paths](../../Docs/virtual_paths.md)) from the pinned Unicode Character
Database in [Unicode/15.1.0](../../Unicode/15.1.0):

- the assigned code points (`DerivedAge.txt`): a segment refuses any other;
- the simple case folding (`CaseFolding.txt`, status C and S): two paths that
  differ only by it are case-only siblings.

The Content build runs the generator when the data or the tool changes and
writes `Internal/Generated.UnicodeTables.h` into the source tree, where it is
versioned.

```powershell
python -m unicode_codegen.cli --ucd ../../Unicode/15.1.0 --cpp-out ../../Internal/Generated.UnicodeTables.h
```

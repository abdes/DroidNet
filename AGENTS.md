# Repository editing rules

## Line endings

- Follow `.gitattributes` and `.editorconfig`: LF for text on every OS;
  CRLF for `.bat` and `.cmd` scripts, including mixed-case extensions.
- Preserve encoding, BOM, file permissions, and binary/test-fixture bytes.
- Write the required line endings directly. Do not run a line-ending script
  after each edit; the existing pre-commit setup normalizes affected files.
- For a known mismatch, use `pre-commit run mixed-line-ending --files path/to/file`
  from the repository root. The standard hooks handle LF and CRLF exceptions.
- Do not normalize or stage unrelated files during an ordinary coding task.

## Oxygen

For all Oxygen code and design work (`projects/Oxygen.*`, their examples/tools,
and Oxygen documents under `design/`), read and follow the shared
[Oxygen engineering rules](design/oxygen/RULES.md).

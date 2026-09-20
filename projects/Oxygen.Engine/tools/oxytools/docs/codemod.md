# Codemod

Codemod generates rename patches without modifying source files. Verified C++
edits go into a safe patch; HLSL, data, documentation, and unresolved C++ matches
are proposed separately for review.

## Install

From the Oxygen.Engine directory, install the optional codemod dependencies into
your chosen Python 3.10+ interpreter:

```powershell
$toolPython = python -c "import sys; print(sys.executable)"
uv pip install --python $toolPython --editable './tools/oxytools[codemod]'
```

If an older `oxygen-codemod` distribution is installed, uninstall it before
installing the shared package. The command remains `codemod`; the module entry
point is `python -m codemod`. Ripgrep (`rg`) must be on PATH.

C++ operations also require a compatible libclang shared library and an existing
`.clangd`/compilation database. On Windows the standard LLVM `bin/libclang.dll`
installation is discovered automatically. `--libclang-file PATH` selects an
explicit library. HLSL/data/text-only operations do not load libclang.

## Preview and generate patches

Run from the desired project root, or select it with `--root`. Substitute your
symbol names in these examples:

```powershell
codemod rename --from OldSymbol --to NewSymbol --kind class --dry-run
codemod rename --from OldSymbol --to NewSymbol --kind class
```

The second command writes `safe.patch` and/or `review.patch` in the current
working directory when there are corresponding edits. Existing output files are
never overwritten. Use `--output-safe-patch` and `--output-review-patch` to choose
other paths. A dry run prints every proposed edit and writes no patches, regardless
of logging verbosity.

Patch paths are relative to the selected edit root. Review both files, then apply
from that root:

```powershell
git apply --check safe.patch
git apply safe.patch
git apply --check review.patch
git apply review.patch
```

Only apply files the command actually produced. The review patch is based on the
result of the safe patch, so apply the safe patch first when both exist. Review
proposals are not asserted to refer to the selected C++ declaration. Build and
test the affected code after applying the changes.

## Select a declaration

Qualified names distinguish scopes, but cannot distinguish function overloads.
When more than one declaration matches, codemod refuses the rename and lists
locations. Select the intended declaration with a location from that list:

```powershell
codemod rename --from 'Example::OldFunction' --to NewFunction --kind function `
  --at 'src/Example.cpp:42:7' --dry-run
```

The location uses a one-based line and UTF-8 byte column, as reported by Clang.
It must identify a matching declaration, not an arbitrary use. This also resolves
same-named local variables. For C++, `--to` must be an unqualified identifier;
renaming does not move declarations between namespaces or classes.

Class renames cover constructors, destructors, and type references. Virtual
method renames include their known override chain. Missing declarations outside
the edit scope, conflicting symbol identities across contexts, name collisions,
and parsing failures stop output generation. The tool reparses the proposed C++
changes before writing patches; it never retries using guessed compiler flags.

## Scope and compilation

Discovery respects `.gitignore`. Additional `--include` and `--exclude` patterns
use gitignore-style globs relative to the edit root, with forward slashes.
An include pattern cannot restore a Git-ignored file.

```powershell
codemod rename --from OldSymbol --to NewSymbol `
  --include 'src/Oxygen/Base/**' --exclude '**/Test/**' --dry-run
```

An explicit `--root` is never widened. An ancestor `.clangd` can still supply its
compilation configuration. Without `--root`, discovery prefers an ancestor with
`.clangd`, then the nearest project marker, then the working directory.

C++ analysis uses recorded translation units under the root, excluding paths
matched by `--exclude`. `--include` restricts edits, not the consumers needed to
analyze headers. Headers are analyzed through those consumers; headers that no
recorded translation unit reaches cause an error. Choose a root and exclusions
that contain the intended declaration and its consumers.

Compilation handling is shared with oxytidy: one unconditional `.clangd`
`CompileFlags` block, response-file expansion, operand-aware flag removal, and
explicit configuration selection. See [compile-command handling](oxytidy.md#configuration-and-compile-command-fidelity).
`--configuration` defaults to `Debug`; use `all` for metadata-free databases or to
analyze every recorded configuration. `--build-dir` overrides the database path.
Required generated headers and response files must already exist.

## Review proposals

| Input                            | Behavior                                                                                                         |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| C++                              | Verified symbol references go to the safe patch; unresolved code tokens are review proposals                     |
| HLSL/HLSLI                       | Whole-token matches outside comments and literals are review-only                                                |
| JSON and JSON-formatted `.scene` | Complete matching string keys/values are review-only                                                             |
| YAML/YML                         | Matching plain or quoted string scalars are review-only; tags, anchors, and block scalars require manual changes |
| Markdown, text, reStructuredText | Whole-token review proposals with `--mode aggressive`                                                            |

`--update-strings` additionally proposes C++/HLSL comment and string-literal edits
in the review patch. It does not make those matches semantically verified. The
default `safe` mode excludes documentation/text files; both modes keep non-C++
proposals out of the safe patch.

When a qualified name such as `Example::Old` is mapped to the unqualified `New`,
matching qualified names in data/text retain their prefix: `Example::New`.
JSON/YAML are checked before and after the proposed edits, including duplicate
keys. Surrounding text, UTF-8/BOM, and line endings are preserved. Conflicting
replacements or source changes during the run prevent patch publication.

Some macro or dependent-reference renames need manual work. If the verified edits
would break parsing, the command stops rather than producing a partial safe
patch. Successful parsing only covers the selected compilation contexts; the
review patch and other build configurations still need human review.

## Options and exit status

`--kind` accepts `class`, `function`, `variable`, `member`, or `namespace`.
`--verbose 0` shows compilation progress; `--verbose 1` (or bare `-v`) also shows
compile commands. `--no-color` and `NO_COLOR` disable colors. Run
`codemod rename --help` for the complete argument list.

| Exit | Meaning                                                                                                |
| ---- | ------------------------------------------------------------------------------------------------------ |
| 0    | Preview or patch generation succeeded, including no matching changes                                   |
| 2    | Invalid input, ambiguity, discovery/parsing failure, conflicting edits, stale input, or output failure |
| 130  | Cancelled                                                                                              |

For implementation details, see [the design](codemod-design.md). Development
checks use the [shared test instructions](../README.md#verification), with the
codemod optional dependencies installed.

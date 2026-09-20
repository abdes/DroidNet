# Codemod design

## Command boundary

`codemod rename` plans edits and publishes patch artifacts. It never applies
patches, stages files, creates commits, or rewrites source files. The explicit
edit root bounds modifications; compilation configuration may come from an
ancestor without widening that boundary.

The CLI owns argument validation and user output. `RenameRefactoring` coordinates
source discovery, backend planning, patch rendering, and publication. The command
shares compilation-database handling and atomic-file helpers with the other
Oxygen tools. Libclang and glob matching are optional package dependencies.

## Discovery and source snapshots

Ripgrep performs literal, whole-word candidate searches and returns NUL-delimited
paths. Searching the unqualified component of a C++ name finds both qualified and
unqualified uses. Gitignore filtering happens before additional include/exclude
filters, so include globs cannot reintroduce ignored files. Discovery errors are
failures, not empty results.

`Sources` stores original UTF-8 bytes. Edits carry a file, byte offset, original
text, replacement text, and optional review reason. All replacements are checked
against the snapshot before rendering. Duplicate replacements collapse;
overlapping or disagreeing replacements fail. Source and compilation-configuration
changes during planning prevent publication.

## C++ identity and coverage

The C++ driver parses recorded translation units with adapted compile commands.
It captures reached project headers and analyzes the captured bytes. There is no
bare-flags fallback. Compilation errors and missing source/header coverage stop
output generation.

Each AST is reduced to declaration/reference records and released before the
next context, rather than retaining every translation unit in memory.

Declaration selection uses canonical Clang USRs. File-local identities also
include the resolved declaration path: Clang's basename-only identifiers must
not merge unrelated declarations in different `same.cpp` files. A qualified spelling can still
identify multiple overloads; `--at` selects one declaration by location. Constructors,
destructors, and template specializations map to their owning type/template.
Virtual overrides form a connected rename group. Declarations outside the edit
scope and collisions with existing names are rejected.

Reference observations are combined across selected contexts. A source location
that resolves to incompatible declarations is not rewritten. Verified edits are
reparsed in memory before patch output. Unresolved code tokens remain explicit
review proposals; known references to other declarations are not renamed.

## Non-C++ proposals

HLSL uses token matching with comment/literal exclusion. Explicit string/comment
updates and documentation changes remain review-only. Text edits replace matched
spans rather than entire lines. JSON and YAML operate on complete string values,
preserve surrounding syntax, and reject malformed data or duplicate keys.
Tagged, anchored, and block YAML scalars are not rewritten automatically.

These backends cannot prove C++ symbol identity. Their edits never enter the safe
patch, even when token or scalar matching is exact.

## Patch artifacts

`PatchGenerator` constructs Git-compatible, project-relative unified diffs. It
quotes special filenames, preserves source bytes and newline conventions, and
emits missing-final-newline markers where needed. The review diff starts from
the safe diff's result so the two patches can be applied in that order.

Both artifacts are rendered before publication. Output paths must differ and
must not already exist. The writer reserves output paths, publishes prepared
contents, and removes its own artifacts on failure when they have not been
changed concurrently. The source snapshots are checked again before returning.
Any artifact that cannot be safely removed is identified in the failure message.

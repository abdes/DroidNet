# Oxygen Codemod Tool — Design & Requirements

## Overview

This document specifies the design and functional requirements for an automated codemod tool tailored to the Oxygen.Engine codebase. The tool's purpose is to perform large-scale, semantically-correct refactorings (renames and related edits) reliably across C++ sources, headers, HLSL/HLSLI shader files, YAML/JSON scene files, and example/demo code.

The guiding principle: prefer semantic, AST-aware renames where possible; fall back to conservative, audited text-based transforms only when necessary.

## Goals

- Provide a single, auditable, repeatable mechanism to perform repository-wide refactorings (rename variable, class, namespace, function, class member).
- Preserve correctness: update all definitions and usages across languages and file types.
- Keep changes atomic and reviewable: produce a single patch/branch per refactor operation.
- Support inclusions/exclusions and dry-run previews for safety.
- Uses ripgrep/rg to accelerate finding target files to process.

## Scope

 1. C++ (.h, .hpp, .hh, .cpp, .cxx) : clang AST based
 2. HLSL / HLSLI (.hlsl, .hlsli) : token/regex-based driver (no AST requirement)
 3. YAML / JSON / other text-based scene files and config manifests : JSON read/write based with full key spec using 'dot' notation
 4. Markdown files where symbol usage appears : precise regex based, with heuristics to derive the regex from common use scenarios of the keyword to replace (such as spacing before/after, quoting with '`' or ''', etc...)

## Requirements

The requirements below are numbered for traceability. Mandatory requirements are prefixed with MUST; recommended features are SHOULD.

### Functional requirements

REQ01: [DONE] MUST: Support renaming a variable (local, global, static) across the entire repo while preserving language semantics and avoiding accidental renames of unrelated tokens.
REQ02: [DONE] MUST: Support renaming a class (type) and update all references — typedefs, template instantiations, forward declarations, inheritance lists, `using` declarations, and constructor/destructor names.
REQ03: [DONE] MUST: Support renaming a namespace and update fully-qualified references, using-directives, and `using namespace` occurrences in headers and implementation files.
REQ04: [DONE] MUST: Support renaming a function (including overloaded functions) and update all call sites; must handle overload resolution where possible. (Semantic USR matching)
REQ05: [DONE] MUST: Support renaming any class member (methods, fields, static members, enum constants) and update both definition and all uses, including pointer-to-member expressions and reflection-like string uses when detected.
REQ06: [DONE] MUST: Honor `.gitignore` and never process files ignored by git.
REQ07: [DONE] MUST: Support additional file/directory exclusions/inclusion via glob patterns provided by the user.
REQ08: [DONE] MUST: Provide a dry-run / preview mode that reports all planned edits (file, line, original snippet, replacement) without modifying files.
REQ09: [DONE] MUST: Produce a single aggregated patch (git patch) containing all edits.
REQ10: [DONE] MUST: NOT edit files, or alter in any form the source tree. Its expected output is the patch file (safe patch) and an optional need-review patch.

## Non-functional requirements

REQ11: [DONE] MUST: Be scriptable and available as a CLI executable in the repo (`tools/codemod/codemod`), returning non-zero exit codes on fatal errors.
REQ12: [DONE] MUST: Produce minimal edits in the generated patch file that do not alter source format.
REQ13: [DONE] MUST: Produce deterministic output given identical input, mapping, and exclusion settings.
REQ14: [DONE] SHOULD: Log all operations on files such as exclusion, processing, summary stats of replaced tokens.
REQ15: [PARTIAL] SHOULD: Where possible, use language-aware parsing for non-C++ files (HLSL tokenization, YAML AST) to avoid textual false-positives. (Regex with word boundaries used)

## Edge cases & correctness

REQ16: [DONE] MUST: Avoid changing code inside string literals and comments except when explicitly requested (`--update-strings`), and log such occurrences separately for manual review. (AST avoided them; HLSL uses heuristic)
REQ17: [DONE] MUST: Avoid accidental renames of partial tokens (e.g., renaming `intensity` should not rename `max_intensity_threshold` unless intended) — support whole-token matching and configurable token boundaries.
REQ18: [DONE] SHOULD: For templated code and dependent names, prefer AST-based transformations; when AST-based resolution is impossible, do not emit the edit.

## Architecture

The tool is divided into these components:

- CLI front-end: parse args for a single rename operation (`--from`/`--to`/`--kind`), include/exclude globs, and operation modes (dry-run, safe, aggressive).
- Symbol enumerator: runs targeted searches using 'rg' (ripgrep) to create an occurrence map of candidate tokens and contexts.
- Backend drivers: per-language transformation drivers:
  - C++ driver (AST-first): integrates with libclang/clang-rename and generates change sets.
  - HLSL driver: token/regex-based driver with conservative matching rules; no clang/libclang dependency for HLSL.
  - YAML/JSON driver: loads scene files, updates keys/values where keys represent serialized field names.
  - Text driver: regex-based fallback for miscellaneous files (docs, markdown), only used in aggressive mode or when explicitly enabled.
- Patch generator: creates a git patch file for safe edits, and git patch files for need-review edits.

## Implementation details and heuristics

- Prefer `clang-rename` or `libclang` bindings to perform C++-level renames — these tools understand scope, templates, and overloads.
- For class renames, ensure constructor/destructor names are updated, and update `using` aliases and typedefs.
- For method/member renames, update vtable-impacting signatures carefully; if a public ABI change is detected, require explicit confirmation and document the ABI impact.
- For namespace renames, update fully-qualified references and update `using` declarations; do not attempt to change macro-generated fully-qualified names without manual review.
- Use regex rename to replace occurences of the renamed token in code comments.

## Tentative CLI (matches requirements)

This CLI is a proposed interface focused on producing patch files only (the tool DOES NOT modify files). It supports mapping files and exclusions per the requirements.

Example usage (dry-run, produce patches) — single-symbol rename:

```text
tools/codemod/codemod --from Sun::intensity --to sun_illuminance_lx --kind member \
  --include "src/**" --exclude "**/third_party/**" \
  --output-safe-patch=codemod-safe.patch \
  --output-review-patch=codemod-review.patch \
  --mode=safe --dry-run
```

Key flags:

- `--from <symbol>` : fully-qualified source symbol to rename (e.g., `Sun::intensity` or `exposure`).
- `--to <symbol>` : target symbol name (e.g., `sun_illuminance_lx`).
- `--kind <member|function|class|namespace|variable>` : optional hint for the symbol kind to improve accuracy.
- `--include <glob>` : repeatable; include files matching the glob (if omitted, defaults to repo root) — this complements `--exclude`.
- `--exclude <glob>` : repeatable; exclude files matching the glob (REQ07).
- `--dry-run` : emit preview and audit reports without making changes (REQ08).
- `--output-safe-patch <file>` : path to write the generated safe patch (REQ09, REQ10).
- `--output-review-patch <file>` : path to write a human-review patch containing lower-confidence or ambiguous edits.
- `--mode {safe,aggressive}` : `safe` prefers AST-first edits; `aggressive` enables regex fallbacks (REQ14).

Note: The codemod backend is automatically selected per-file based on file type (C++ uses the clang/AST driver; HLSL uses the HLSL driver; YAML/JSON use the content driver). No `--backend` flag is required.

Behavior note: the tool only generates patch files and does not modify the working tree; it will produce patches regardless of uncommitted changes in the working tree.

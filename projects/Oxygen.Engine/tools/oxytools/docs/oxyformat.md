# Oxyformat

## Run the command

Install the [shared tools package](../README.md) once in your selected Python
environment. From the engine directory:

```powershell
oxyformat src/Oxygen/Base
oxyformat src/Oxygen/Base --fix
oxyformat src/Oxygen/Base/Uuid.cpp src/Oxygen/Base/Uuid.h
oxyformat --all
```

`python -m oxyformat` and `tools/cli/oxyformat.ps1` expose the same options. The
PowerShell launcher selects the active virtual environment or Python on PATH
without installing or updating packages. No arguments displays usage.

Scope paths are relative to the engine root, matching oxytidy. `--project-root`
selects another root relative to the caller's directory. `--paths-from-cwd`
explicitly switches input paths to the caller's directory; the hook uses this for
Git's repository-relative filenames. There are no Git operations in oxyformat.

The formatter is discovered on PATH, then in the standard Windows LLVM install
directory. `--clang-format-bin PATH` selects another executable; its version must
still be 22.x. `--jobs N` defaults to min(CPU count, 8); `--timeout SECONDS`
defaults to 30 seconds per LLVM invocation.

Eligible extensions are `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx`,
`.inc`, `.inl`, `.ipp`, `.tpp`, `.ixx`, and `.cppm`. Overlapping paths are
deduplicated. Explicit excluded or non-C++ files are counted as skipped; missing
paths are failures. Directory traversal prunes excluded subtrees and does not
follow directory symlinks. Generated files outside owned roots are excluded;
generated files inside an owned root must be covered by the shared policy.

## Scope and style

Tests are included by default. Ownership and exclusions come from the engine
root's `.oxytools.json`; every file uses that root's `.clang-format`, including
files beneath a nested style file. Per-directory ignore files do not override
the shared ownership policy. There are no style overrides, Git selectors, or diff
mode. Source-level clang-format off/on directives remain effective.

Clang-format 22.x is required. No build tree or compilation database is needed.
The selected root style is validated once and snapshotted for the run.

## Results and failures

Checking is read-only. `--fix` formats independent files eagerly and continues
after individual failures. Failed files are not rewritten; successful writes use
atomic replacement per file, with concurrent-edit checks before replacement.
Run-wide prerequisite failures stop processing before writes. Cancellation stops
dispatch and terminates active formatters; completed writes are retained.

| Exit | Meaning                                                                                             |
| ---- | --------------------------------------------------------------------------------------------------- |
| 0    | All selected files were compliant or successfully formatted; also used when every input is excluded |
| 1    | Check mode found files needing formatting                                                           |
| 2    | Input, configuration, or execution failure; other files may have succeeded                          |
| 130  | Cancelled                                                                                           |

Interactive output groups affected paths under one heading and ends with the
outcome, relevant counts, and elapsed time. Compliant files are not listed
individually; routine zero counts and redundant totals are omitted. Errors remain
visible, and partial or cancelled runs are not labelled clean. `NO_COLOR` disables
colors.

When redirected, affected file records go to stdout with full absolute paths on
single lines. Context, failures, and summaries go to stderr. Redirected output
has no color or animation. Oxytidy uses the same presentation conventions, with
additional analysis context available through its `--verbose` option.

## Checking hook

The repository root's single pre-commit configuration checks supplied C++ files
under the engine's source and example trees. The shared ownership policy applies
its exclusions inside the command. The hook never passes `--fix` or stages files.
Pre-commit temporarily stashes unstaged changes and restores them after checking
the staged contents.

On failure, run `oxyformat <paths> --fix`, stage the intended changes, and retry
the commit. With partially staged files, stage only the formatting changes you
intend to commit. The hook is installed through the repository's existing
`pre-commit install` workflow.

The hook executes this checkout's Python source in a cached pre-commit environment
with PyYAML, jsonschema, and Rich. Dependencies are installed once; no package
manager or PowerShell launcher runs on each commit. It processes only supplied
filenames and does not scan the project. Pre-commit dispatches batches serially;
oxyformat owns bounded parallelism within each batch. There is no persistent
formatting-result cache.

Changing the policy or style does not trigger a repository-wide hook scan. Run
`oxyformat --all` explicitly after changing those rules.

# Oxyformat

## Usage

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

## Agreed contract

`oxyformat` checks C++ formatting by default. `--fix` eagerly formats independent
files, continuing after individual failures. Explicit files, recursive
directories, and `--all` are supported; there are no Git selectors or diff mode.
Tests are included. Ownership and exclusions come only from the engine root's
`.oxytools.json`; formatting comes only from that root's `.clang-format`.
Clang-format 22.x is required. No build tree or compilation database is needed.

Every file uses the root style, including files beneath a nested `.clang-format`.
Per-directory ignore files do not override the shared ownership policy. Style
overrides are not exposed. Clang-format off/on directives inside source remain
part of the source's formatting contract.

Individual file failures are reported and processing continues. A failed file is
not rewritten; successful writes are atomic per file. Concurrent source edits
are detected before replacing a file. Run-wide prerequisite failures stop the
run before writes. Cancellation stops dispatch and terminates active formatters.
Successful writes already completed before cancellation are retained.

Check mode lists files needing formatting and failures. Fix mode lists changed
files and failures. Already compliant files appear only in summary counts.
No formatted source or diff is printed. Exit codes are 0 for success, 1 for
formatting needed, 2 for execution/configuration failures, and 130 for cancellation.

## Performance and delivery gates

The hook processes only supplied filenames; it never scans the project to find
additional work. It installs dependencies once, not during each commit. The
command checks the tool version and root configuration once per invocation and
uses bounded workers. No persistent cache is required for correctness.

Before adding the checking hook, validate unit and real-LLVM tests, verify a real
module's check/fix/check/idempotence cycle, restore the verification edits, and
measure cold and repeated runs for small commit-sized inputs and module scope.
Afterward, validate the hook independently, including partially staged files and
restoration of unstaged content. The hook never writes or stages source files.

## Validation evidence

On Windows with Python 3.14.3 and LLVM 22.1.8, the command's automated suite and
the existing oxytidy suite passed during standalone command validation. A real Base run
checked 74 files, formatted three, then passed check and idempotence runs. All
three files were restored byte-for-byte; no C++ formatting edits are included
with the tool implementation.

Five fresh-process checks per input size, including Python startup and without a
persistent result cache, measured:

| Files | First run | Median | Maximum |
| --- | --- | --- | --- |
| 1 | 315 ms | 333 ms | 381 ms |
| 10 | 417 ms | 417 ms | 444 ms |
| 50 | 716 ms | 716 ms | 742 ms |
| 74 | 856 ms | 773 ms | 856 ms |

Reproduce with `python tools/oxytools/scripts/benchmark_format.py`. These are
local measurements, not cross-machine latency guarantees. Each sample starts a
fresh process; OS disk caches are not flushed. The separate pre-commit/Git layer
adds overhead, especially when stashing partially staged files or creating its
environment on first use. Full repository hook timing is recorded separately
from the formatter's duration.

# Oxygen clang-tidy workflow

Analyze explicit project files, headers, directories, or all project roots.
Report-only is the default. There is no Git commit/index inspection or hook
integration. Python owns the only CLI; shell launchers forward its arguments unchanged.

## Prerequisites

- Python 3.10+ and [uv](https://docs.astral.sh/uv/getting-started/installation/)
  for editable installation into the active virtual environment or default Python. The PowerShell launcher requires PowerShell
  7.3+ for lossless native argument forwarding. LLVM remains a separate prerequisite.
- LLVM 23.x `clang-tidy` and `clang-scan-deps`. Optional formatting also needs
  LLVM 23.x `clang-format`. Other major versions are rejected before analysis;
  minor/patch versions within 23.x may differ. LLVM is installed separately.
  The POSIX process path requires platform validation.
- An existing compilation database, generated headers, and the compiler's normal
  development environment. This tool does not configure or build CMake.
- A configured Ninja build tree for this checkout. Oxygen's existing CMake
  configuration generates `.clangd` from the tracked `.clangd.in` template.
  The generated `.clangd` remains ignored: its build-directory path is local.
  The checked-in template uses `@CMAKE_BINARY_DIR@`, not a private machine path.
  `--clangd-file` can explicitly select an existing configuration. oxytidy only
  detects/reports missing prerequisites; it never runs Conan, configures CMake,
  builds targets, or generates `.clangd` itself.

## Python project and launchers

Oxytidy is part of the shared `oxygen-tools` Python distribution in `tools/oxytools`:

```text
tools/oxytools/
  pyproject.toml       # metadata, dependencies, console entry point
  uv.lock             # optional reproducible dependency resolution for development
  src/oxytidy/        # analysis command
  src/oxyformat/      # formatting command
  src/oxytools/       # shared helpers
  tests/
```

From the engine directory:

```powershell
.\tools\cli\oxytidy.ps1 src/Oxygen/Base --include-tests
```

The launcher selects the interpreter in `VIRTUAL_ENV` when active; otherwise it
uses the default `python` application on PATH. An invalid active environment is
an error, not a reason to silently switch interpreters. It creates no dedicated
runtime environment and does not activate, deactivate, or modify your shell's
environment variables.

The launcher uses `uv pip install --python <selected-interpreter> --editable
<project>` to check/install the package and its dependencies in that interpreter.
Satisfied installations are audited without reinstallation. uv is the installer,
not the runtime/environment selector. The launcher never uses `uv run`, discovers
another `.venv`, or silently creates one. Installation errors stop execution and
identify the selected interpreter. [uv interpreter targeting](https://docs.astral.sh/uv/pip/environments/#using-arbitrary-python-environments)

PowerShell resolves the sibling project from `$PSScriptRoot`, preserves the
caller's working directory, and invokes the selected Python with `-m oxytidy`,
forwarding `$args` unchanged and returning Python's exit status. There are no
PowerShell-specific options. Empty values, spaces, quotes, literal `--`, and
leading dashes are preserved. Quote shell metacharacters as usual.

For installation without the wrapper, use your chosen interpreter directly:

```powershell
python -m pip install -e tools/oxytools
python -m oxytidy src/Oxygen/Base --include-tests
# The console command has the same CLI when its scripts directory is on PATH.
oxytidy src/Oxygen/Base --include-tests
```

Dependencies and metadata come from `pyproject.toml`. An editable installation
tracks the current checkout's Python sources. One interpreter has one installed
version of oxytidy; launching the wrapper from another checkout updates that
editable installation to the requested checkout. A wheel installation locates
the engine from the current directory; editable installs locate their own source
checkout first. An explicit `--project-root PATH` takes precedence over both.

## Usage

Examples below assume the installed console command or the `oxytidy` PowerShell
alias from the engine terminal profile:

```powershell
oxytidy src/Oxygen/Base/Sha256.cpp --summary-only
oxytidy src/Oxygen/Base/Sha256.h --list-files
oxytidy src/Oxygen/Base --configuration Debug --jobs 6
oxytidy --all --configuration Debug --incremental
oxytidy src/Oxygen/Base/Sha256.h --export-fixes out/sha256-fixes.json
oxytidy src/Oxygen/Base/Sha256.h --fix
oxytidy src/Oxygen/Base/Sha256.h --fix --format
oxytidy src/Oxygen/Base/Sha256.cpp --fail-on warning --timeout 60
```

### Select another engine checkout

`--project-root PATH` selects the engine checkout to analyze independently of
where oxytidy is installed or launched. Relative root paths resolve from the
caller's working directory. The directory must already exist; no project or
build tree is created. The selected root owns the source paths, `.clangd`, normal
`.clang-tidy` discovery, and default log/cache locations. The selected `.clangd`
provides the compilation database directory. This analyzes the target checkout's
actual files, not worktree files against a different checkout's database.

Ownership policy selection follows this order:

1. Explicit `--ownership-file PATH` (relative to the caller's directory).
2. `<project-root>/.oxytools.json`, if present.
3. The tool checkout's `.oxytools.json`, using normal tool/run-location discovery.

The third choice supports older target checkouts that do not yet contain the
policy. The fallback path and the missing target policy are printed explicitly.
Paths _inside_ every policy resolve against the selected project root. No policy
is copied into the target checkout. An invalid target policy or a missing/invalid
explicit override fails; fallback applies only when the target policy is absent.
If no policy is available, supply `--ownership-file` explicitly.

This fallback is limited to the ownership policy. The target checkout's build
configuration and source paths are never replaced with those of the tool.

For example, from the engine checkout containing this tool:

```powershell
.\tools\cli\oxytidy.ps1 `
  --project-root "D:/checkouts/Oxygen.Engine" `
  src/Oxygen/Base --include-tests
```

The console command and `python -m oxytidy` accept the same options. Default output
shows mode, scope, build configuration, test inclusion, meaningful overrides,
phase completion, findings, and coverage gaps. Ownership fallbacks remain visible.
`--verbose` adds Python/environment and tool locations, configuration discovery,
compile adjustments, LLVM versions, and artifact/cache details. `--summary-only`
hides individual findings independently of verbosity. Detailed provenance remains
available in `summary.json` and per-invocation artifacts in both modes.
Compilation database paths are never rewritten or remapped.

Use `--clang-tidy-bin`, `--clang-scan-deps-bin`, and `--clang-format-bin` to select
executables. Companion tools are also discovered beside clang-tidy. `--help`
lists every option, identically through every launcher.

Scope paths and configuration overrides are relative to the selected project
root. Explicit build, log, cache, ownership-policy, and export paths are relative
to the caller's directory. Databases from another
checkout are never silently remapped. No scope prints usage; Vortex is no longer
an implicit default.

## Ownership and coverage

The shared [repository ownership policy](../../README.md#repository-ownership-policy),
`.oxytools.json`, owns the authored project roots (`src/Oxygen`, `Examples`) and
vendored exclusions. Update it when adding embedded third-party code. Bundled
sigslot, NamedType internals, loguru implementation, cgltf, stb, and ImGui backends
are excluded, as are conventional vendor/external directories.

Third-party/system headers and generated build outputs remain compiler
dependencies; they are not analysis targets or editable files. Canonical paths
are checked against ownership and explicit scope after resolving symlinks.

| Selection   | Coverage and reported findings                                            |
| ----------- | ------------------------------------------------------------------------- |
| Source file | Its compilation contexts; findings in that file                           |
| Header      | Consumers beneath its containing directory; findings in that header       |
| Directory   | Translation units beneath that directory; findings in its sources/headers |
| `--all`     | All configured authored project roots                                     |

Test path components are excluded, case-insensitively, from selection, requested
headers, reporting, and fixes unless `--include-tests` is supplied.

Discovery is bounded by the supplied inputs. Directory inputs admit only
translation units under those directories. Source-file inputs admit only those
files. Standalone headers use their containing directory as the discovery
boundary, without widening the reporting/edit scope. Multiple inputs combine
these boundaries; `--all` explicitly selects all owned project roots. Boundaries
are recorded before discovery and printed with `--verbose`. No downstream or project-wide
consumer search is performed implicitly.

Fresh compiler dependency output determines which requested headers these
contexts reach. Third-party/system dependencies can still be read by the
compiler. They are not candidate translation units, reported files, or editable
targets. Directory selection validates headers in the module's own compilation
contexts; it does not promise validation in every downstream consumer context.

Headers not reached by the scoped contexts are reported separately as module
coverage gaps. They do not change the exit policy or block fixes to analyzed
code. The module must provide appropriate source/test consumers. If nothing can
be analyzed, the result is `no_analysis` with exit 0, never `clean`; no fixes are
applied. Discovery failures make header reachability uncertain and remain actual
tool failures. Missing compile commands for explicitly scoped source files also
remain failures.

Selecting an excluded file fails. `--max-files` remains a diagnostic cap:
omitted analysis contexts make the run incomplete and prevent autofix.

`--list-files` discovers dependencies and validates configuration without running
tidy checks. It shows selected contexts, selection reasons, and coverage gaps.

## Configuration and compile-command fidelity

Supported `.clangd` input is exactly one unconditional `CompileFlags` block with
`CompilationDatabase`, `Add`, `Remove`, and optional `Compiler`. YAML block/inline
lists and scalar Add/Remove values work. Duplicate keys, conditional flags,
multiple flag fragments, unknown fields, and unsupported removal grammars fail
explicitly. Other editor blocks do not supply tidy checks; `.clang-tidy` does.

Entries can use `arguments` or platform-quoted `command`. Relative source paths
resolve against each entry's directory. Nested response files expand there;
UTF-8/UTF-16 BOMs are supported and cycles are rejected. Compiler identity is
retained unless explicitly overridden.

Response files in `.clangd` Add and clang-tidy ExtraArgs/ExtraArgsBefore are also
expanded against the compilation directory. Their contents participate in input
snapshots and cache validity, so changing a macro inside a response file cannot
reuse analysis for the old macro value.

Remove supports exact flags, trailing-prefix wildcards, and bounded operand
rules for include paths, definitions, forced includes, outputs/dependencies,
language/target/sysroot options, and common MSVC equivalents. The supported
operand table is in the shared `oxytools/compilation.py`. Operands, aliases, and
associated `-Xclang` markers are removed together. Unknown operand grammar fails instead of silently
changing the command. Add runs after Remove.

Debug is the default configuration. Identity is established through
`CMAKE_INTDIR` or standard configuration components in the output path. Unknown
identity fails. Use `--configuration all` explicitly for metadata-free databases
or to retain every configuration. Equivalent commands are deduplicated; distinct
commands for the same source remain separate contexts.

Entries identified as another configuration are skipped before response-file
expansion, so selecting Debug does not require generated response files from
Release or RelWithDebInfo. If recorded arguments and output do not establish the
configuration, response files are expanded to discover it. Missing response files
for selected entries, unresolved configurations, or `--configuration all` still
fail explicitly.

LLVM discovers/inherits the nearest `.clang-tidy`; the effective configuration
is snapshotted and verified. Header filters use LLVM-compatible regexes and
compiler path spellings, including `..` paths. Project header exclusions remain
effective; system-header diagnostics are disabled. `--checks` appends to the
configured checks; `--checks=-*,desired-checks` replaces the enabled set.
Whitespace around dumped check globs is normalized before validation, including
folded YAML lists combined with CLI overrides. Check order and names are
preserved, and unknown checks still fail validation.

## CLI presentation

Argument parsing remains Python's standard `argparse`. Maintained
[rich-argparse](https://github.com/hamdanal/rich-argparse) supplies grouped help;
[Rich](https://rich.readthedocs.io/en/stable/console.html) supplies terminal-aware
layout, colors, tables, error panels, and
[progress displays](https://rich.readthedocs.io/en/stable/progress.html).
There is no custom ANSI renderer, terminal-width calculator, or progress engine.

The output follows the [CLI Guidelines](https://clig.dev/#output):

- Setup errors lead the output, followed by the run context and report location.
- Trace, progress, and errors go to stderr. Diagnostics and file listings go to
  stdout; redirected diagnostics preserve full absolute paths on single lines.
- Interactive terminals get Rich's progress and styled panels. Redirected output
  has no animation or terminal control codes. Rich handles `NO_COLOR` and terminal
  capabilities. No prompt is required.
- Default terminal paths are project-relative. `--verbose` defines `@project`,
  `@tool` when different, `@python`, and `@run` aliases in a Paths section.
  Paths wrap instead of being truncated, and JSON artifacts retain full paths.
- `--summary-only` suppresses individual diagnostics, not failures or coverage
  gaps. Every unreached header and every analysis gap is listed directly in the
  terminal, with no truncation in either verbosity mode. These are results needed
  to judge coverage, not optional startup detail. JSON retains the same lists.
- Both tools share the same header and outcome styling. Routine zero counters
  are omitted. Full configuration and execution traces remain available through
  `--verbose` and the run artifacts.

## Results and execution

| Exit | Meaning                                                                                                                  |
| ---- | ------------------------------------------------------------------------------------------------------------------------ |
| 0    | Successful analysis/listing, or no analyzable header consumers in scope                                                  |
| 1    | Findings fail `--fail-on warning`/`error`, or clang-tidy reports a configured `WarningsAsErrors` policy failure          |
| 2    | Invalid input, execution failure, timeout, incomplete analysis (such as missing compile commands), or rejected fix batch |
| 130  | Cancelled                                                                                                                |

The summary distinguishes `setup_failed`, `clean`, `findings`, `incomplete`,
`cancelled`, `listed`, and `no_analysis`. Setup failures identify the missing prerequisite, its
expected path, and the explicit configuration option. They report **Analysis did
not start**, without a findings count. Ninja configuration generates the local
`.clangd` and compilation database; any required generated headers must also
exist. oxytidy performs none of these setup steps, and no configuration is
invented or borrowed from another checkout.

Summary schema 2 records the failing phase and `analysis_started`. Findings and
level counts are null when analysis never started. Completed analyses distinguish
clean results from findings; failures after analysis starts label results as
partial. File listing explicitly reports that analysis was not requested. `--fail-on none` is advisory but never suppresses execution/coverage
failures. Compilation errors outside scope still make analysis incomplete.
Configured `WarningsAsErrors` remains a failing findings policy, rather than an
execution failure, and does not prevent coordinated fixes.

Workers default to min(CPU count, 8). Completed contexts report paths, durations,
and status. Unique findings and notes appear promptly unless `--summary-only`
is set. Repeated findings retain originating contexts. Cancellation stops
dispatch, terminates owned process trees, and preserves completed results.
`--timeout` applies to each LLVM invocation, including discovery/preflight.

Every run gets a unique UTC-stamped subdirectory under `out/clang-tidy` or the
parent supplied through `--log-dir`. Every context has distinct commands,
configuration, dependency information, raw logs, and structured YAML diagnostics.
`summary.json` records options, tool versions/hashes, coverage, counts, durations,
diagnostics, and fixes. Post-fix verification retains separate artifacts.

## Coordinated fixes

Parallel workers export replacements and never perform in-place fixes. The
complete batch is validated before editing. Identical edits are deduplicated;
overlaps, conflicting contexts, changed analyzed contents, and incomplete
analysis block application. Each diagnostic's replacements are skipped as a
unit if any edit crosses scope. Skipped/unfixable findings retain reasons.

UTF-8 bytes, BOM, existing line endings, and permission bits are preserved.
Other source encodings can be analyzed, but an attempted autofix on an unsupported
encoding rejects the replacement batch. Writes are atomic per file. Failed
application rolls back written files unless concurrent edits or I/O failures
prevent restoration, which is reported. Optional formatting uses `.clang-format`
and changed ranges. After edits, affected caches are invalidated and all selected
contexts are freshly scanned/analyzed. Verification determines the final result.

`--export-fixes PATH` writes a reviewable JSON plan with replacements, skipped
reasons, and content hashes, without editing sources. Existing exports are not
overwritten. LLVM YAML exports remain available per context in both modes.

## Incremental reuse

`--incremental` caches successful invocation results. Fresh dependency scans
detect newly resolved includes, including `__has_include` changes. Reuse requires
matching transitive file contents, effective configuration, compile arguments,
LLVM identities, relevant environment, and tool code. No timestamp-only validity
decisions are used. Scope and failure policy are evaluated on each run. Failed
or cancelled invocations are never cached. `--force` bypasses reuse and
`--cache-dir` relocates it. The console reports the total context count and any
nonzero reuse count; `summary.json` records executed and reused counts separately.
Cache envelopes and nested diagnostics are validated before reuse. Invalid cache
entries are reported and recomputed; they cannot crash report rendering.

## Implementation and verification

Use the [shared verification instructions](../README.md#verification) to install
this checkout, select a suitable working directory, and run tests and linting.
The same suite covers the commands and shared helpers.

The `oxytidy` modules own analysis/cache, diagnostics, replacement planning, and
orchestration. The `oxytools` package owns shared
compilation adaptation, ownership validation, file writes, process ownership, and
presentation primitives.

Integration tests discover LLVM on PATH or in the standard Windows installation.
`OXYTIDY_TEST_LLVM` selects another clang-tidy executable. Tests explicitly skip
if LLVM or PowerShell is unavailable; unit-only success is not integration proof.

Tests cover real header warnings/fixes, external exclusions, shared headers,
basename collisions, compilation contexts, coverage gaps, compile failures,
export/format modes, cache invalidation, YAML/responses, fix conflicts and changed
contents, rollback, failure policies, worker failures, process-tree cancellation,
and PowerShell argument/exit forwarding.

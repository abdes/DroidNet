# Oxygen Console

`Oxygen.Console` provides runtime CVars and a text front end over
[`Oxygen.Commands`](../Commands/README.md): parsing, history, completion and
persistence for engine and module settings. It does not store commands; it
binds console lines to commands in the command registry it is given.

This module is UI-agnostic by design. It does not depend on ImGui or editor code.

## Goals

- Deterministic runtime configuration at engine-defined sync points.
- Pluggable registration from multiple engine modules.
- CVar policy by command source (`local_console`, `config`, `remote`, `automation`, `ui`, `key`).
- Safe persistence for archived CVars and command history.
- Good UX primitives (history, completion, built-ins) without forcing a UI framework.

## Public API Surface

Primary entry point: `oxygen::console::Console` in `src/Oxygen/Console/Console.h`.

Key operations:

- `RegisterCVar(...)`
- `ApplyStartupPlan(...)`
- `Parse(line)`: bind a whole line (commands separated by `;`) to
  invocations, or return the first error without running anything
- `Execute(line, context)`: parse and run a line synchronously (startup,
  `exec`, configuration); an asynchronous command returns `kPending`, because
  only a dispatcher can run it
- `RecordHistory(line)`, `RecordExecution(line, result)`: used by a
  dispatcher that runs lines itself
- `Complete(prefix)` and completion cycling helpers
- `ApplyLatchedCVars()`
- `SaveArchiveCVars(path_finder, mode)` / `LoadArchiveCVars(path_finder, context)`
- `SaveHistory(path_finder)` / `LoadHistory(path_finder)`
- `ApplyCommandLineOverrides(args, context)`
- `SetSourcePolicy(...)`: CVar mutation, `DevOnly` and `Cheat` allowance per
  source
- `Commands()`: the command registry the console runs lines through

## CVar Model

Types:

- `bool`
- `int64_t`
- `double`
- `std::string`

Core flags (`CVarFlags`):

- `kArchive`: persisted to archive file.
- `kReadOnly`: cannot be mutated at runtime.
- `kCheat`: gated by source policy.
- `kDevOnly`: gated by source policy/shipping mode.
- `kRequiresRestart`: stores pending restart value semantics.
- `kLatched`: stage value and apply on `ApplyLatchedCVars()`.
- `kRenderThreadSafe`: explicitly marked safe for render-thread visibility contracts.
- `kHidden`: excluded from normal discovery APIs unless explicitly requested.

Each CVar is represented as a `CVarSnapshot`:

- definition metadata
- current value
- current value origin
- optional latched value
- optional restart value

Value provenance is tracked internally with this precedence ladder:

1. `AppForced`
2. `RuntimeExplicit`
3. `StartupExplicit`
4. `PersistedPreference`
5. `AppDefault`

Terms:

- `AppDefault`: the app/module baseline default or startup-effective value when it is not an explicit override.
- `StartupExplicit`: explicit startup intent for this session, typically from CLI/options/bootstrap code.
- `RuntimeExplicit`: interactive or programmatic runtime mutation after startup.
- `PersistedPreference`: a cached archive preference loaded from disk.
- `AppForced`: a host-owned value that must not be overridden or promoted into the archive.

Registration keeps the default path simple:

- `RegisterCVar(definition)` for normal CVars.
- `RegisterCVar(definition, options)` when a module needs to supply a startup-effective app default or forced value.

Startup-only overrides should be passed through `ConsoleStartupPlan`, not by calling `SetCVarFromText()` during bootstrap.

Handle type:

- `CVarHandle` is a strong named type (not a raw integer).

## Commands

Commands, their parameters, results and command policy belong to
[`Oxygen.Commands`](../Commands/README.md). The console registers its
built-ins into the command registry it is given and unregisters them when it
is destroyed:

- `help [name]`
- `find <pattern>`
- `list [all|commands|cvars]`
- `exec <path>`
- `cvar.get <name>`, `cvar.set <name> <value>`, `cvar.list [pattern]`

Line syntax:

- `name args...`: positional arguments in the command's parameter order, or
  `param=value` for any parameter
- `name value` for a CVar is shorthand for `cvar.set name value`
- commands chained with `;` run in order and stop at the first failure; the
  whole line is bound before any of it runs
- quoted tokens and escapes via the parser

## Completion and History

Completion:

- Case-insensitive prefix completion for commands and CVars.
- Ranking blends prefix quality with usage frequency/recency.
- Cycling APIs for UI adapters (`BeginCompletionCycle`, `NextCompletion`, `PreviousCompletion`).

History:

- In-memory bounded history with adjacent dedupe behavior; only lines typed
  in the console (`local_console`) are recorded.
- JSON persistence support via `SaveHistory`/`LoadHistory`.
- Execution record buffer for UI/log adapters.

## Persistence

Persistence is path-driven through `oxygen::PathFinder`:

- CVar archive path: `PathFinder::CVarsArchivePath()`
- History path: sibling of the archive path, file name `console_history.json`

Formats:

- CVars: JSON with versioned schema and typed value payloads.
- History: JSON with versioned schema and command string entries.

Archive load is passive:

- loading records persisted preferences in the registry
- a persisted value only becomes live if no higher-precedence source already owns the CVar
- late-registered archived CVars still receive cached startup and persisted values in precedence order

Archive save modes:

- `ArchiveSaveMode::kAutomatic`
  - persists live values only for `PersistedPreference` and `RuntimeExplicit`
  - does not promote `StartupExplicit`, `AppDefault`, or `AppForced`
- `ArchiveSaveMode::kExplicit`
  - persists the current live value for all origins except `AppForced`
  - `AppForced` values remain non-promotable

Runtime logging:

- `INFO` on successful save/load.
- `WARNING` for expected non-fatal missing-file cases.
- `ERROR` for filesystem/parse/schema failures.

## Source Policy

Each command source has a CVar policy: whether it may set CVars, and whether
`DevOnly` and `Cheat` CVars are allowed. Command policy and the audit hook
are per source in the command registry.

## Engine Integration Pattern

Recommended orchestration (already used by `AsyncEngine`):

1. Register engine-owned CVars and commands.
2. Apply `ConsoleStartupPlan`.
3. Load persisted archive preferences.
4. Register service/module console bindings.
5. At deterministic frame boundary (FrameStart):
   `ApplyLatchedCVars()` then apply module-owned runtime CVars.

Inversion-of-control pattern for modules:

- Each module registers its CVars in `RegisterConsoleBindings(...)` and its
  commands in `RegisterCommands(...)`.
- Each module maps CVar values to runtime state in `ApplyConsoleCVars(...)`.
- Engine orchestrates timing; modules own semantics.

## UI Integration

Keep module layering clean:

- `Oxygen.Console`: no UI dependency.
- `Oxygen.ImGui`: UI widgets/panels for console and palette.
- Host: posts console and palette lines to the command dispatcher
  (`Oxygen.Control`), and owns layout and persistence of panel geometry.

This keeps runtime console functionality usable in non-ImGui and non-editor builds.

## Minimal Example

```cpp
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/Console.h>

using namespace oxygen::console;

oxygen::commands::CommandRegistry commands {};
Console console { oxygen::observer_ptr { &commands } };

(void)console.RegisterCVar(CVarDefinition {
  .name = "ngin.target_fps",
  .help = "Target FPS (0 = uncapped)",
  .default_value = int64_t { 0 },
  .flags = CVarFlags::kArchive,
  .min_value = 0.0,
  .max_value = 1000.0,
}, CVarRegistrationOptions {
  .initial = StampedCVarValue {
    .value = int64_t { 120 },
    .origin = CVarValueOrigin::kAppDefault,
  },
});

ConsoleStartupPlan startup_plan {};
startup_plan.Set("ngin.target_fps", int64_t { 30 });
console.ApplyStartupPlan(startup_plan);

const auto result = console.Execute("ngin.target_fps 60");
```

## Testing

Tests live under `src/Oxygen/Console/Test` and cover:

- parser behavior and edge cases
- CVar conversion, bounds, and policy gating
- line binding, execution and built-ins
- completion and history behavior
- persistence round-trips

## Notes

- If a setter has side effects, make it idempotent. `ApplyConsoleCVars()` is expected to run at a fixed frame sync point.
- Prefer module-scoped naming prefixes for symbols (for example `ngin.*`, `gfx.*`, `nput.*`, `cntt.*`).

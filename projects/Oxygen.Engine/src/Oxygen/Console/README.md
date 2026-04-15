# Oxygen Console

`Oxygen.Console` provides runtime CVars, command execution, history, completion, and persistence for engine and module settings.

This module is UI-agnostic by design. It does not depend on ImGui or editor code.

## Goals

- Deterministic runtime configuration at engine-defined sync points.
- Pluggable registration from multiple engine modules.
- Strong policy control by command source (`local`, `cfg`, `remote`, `automation`).
- Safe persistence for archived CVars and command history.
- Good UX primitives (history, completion, built-ins) without forcing a UI framework.

## Public API Surface

Primary entry point: `oxygen::console::Console` in `src/Oxygen/Console/Console.h`.

Key operations:

- `RegisterCVar(...)`
- `ApplyStartupPlan(...)`
- `RegisterCommand(...)`
- `Execute(line, context)`
- `Complete(prefix)` and completion cycling helpers
- `ApplyLatchedCVars()`
- `SaveArchiveCVars(path_finder, mode)` / `LoadArchiveCVars(path_finder, context)`
- `SaveHistory(path_finder)` / `LoadHistory(path_finder)`
- `ApplyCommandLineOverrides(args, context)`
- `SetSourcePolicy(...)`, `SetRemoteAllowlist(...)`, `SetAuditHook(...)`

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

## Command Model

Command metadata (`CommandDefinition`):

- `name`
- `help`
- `flags`
- `handler(args, context) -> ExecutionResult`

Command flags:

- `kDevOnly`
- `kCheat`
- `kRemoteAllowed`

Execution result contract:

- `status` (`kOk`, `kNotFound`, `kInvalidArguments`, `kDenied`, `kError`)
- `exit_code`
- `output`
- `error`

## Built-in Commands

Built-ins registered by `Registry`:

- `help [name]`
- `find <pattern>`
- `list [all|commands|cvars]`
- `exec <path>`

Also supported:

- command chaining with `;`
- quoted tokens and escapes via parser

## Completion and History

Completion:

- Case-insensitive prefix completion for commands and CVars.
- Ranking blends prefix quality with usage frequency/recency.
- Cycling APIs for UI adapters (`BeginCompletionCycle`, `NextCompletion`, `PreviousCompletion`).

History:

- In-memory bounded history with adjacent dedupe behavior.
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

## Source Policy and Security

Policies are configured per command source:

- `kLocalConsole`
- `kConfigFile`
- `kRemote`
- `kAutomation`

Each source policy controls:

- command execution allowance
- CVar mutation allowance
- `DevOnly` allowance
- `Cheat` allowance

Remote hardening:

- remote allowlist for permitted subjects
- optional audit hook (`AuditEvent`) for telemetry/compliance pipelines

## Engine Integration Pattern

Recommended orchestration (already used by `AsyncEngine`):

1. Register engine-owned CVars/commands.
2. Apply `ConsoleStartupPlan`.
3. Load persisted archive preferences.
4. Register service/module console bindings.
5. At deterministic frame boundary (FrameStart):
   `ApplyLatchedCVars()` then apply module-owned runtime CVars.

Inversion-of-control pattern for modules:

- Each module registers its own commands/CVars in `RegisterConsoleBindings(...)`.
- Each module maps CVar values to runtime state in `ApplyConsoleCVars(...)`.
- Engine orchestrates timing; modules own semantics.

## UI Integration

Keep module layering clean:

- `Oxygen.Console`: no UI dependency.
- `Oxygen.ImGui`: UI widgets/panels for console and palette.
- Demo/app shell: hotkeys, layout, persistence of panel geometry.

This keeps runtime console functionality usable in non-ImGui and non-editor builds.

## Minimal Example

```cpp
#include <Oxygen/Console/Console.h>

using namespace oxygen::console;

Console console {};

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

(void)console.RegisterCommand(CommandDefinition {
  .name = "ngin.ping",
  .help = "Health check command",
  .flags = CommandFlags::kNone,
  .handler = [](const std::vector<std::string>&, const CommandContext&) {
    return ExecutionResult {
      .status = ExecutionStatus::kOk,
      .exit_code = 0,
      .output = "pong",
      .error = {},
    };
  },
});

const auto result = console.Execute("ngin.ping");
```

## Testing

Tests live under `src/Oxygen/Console/Test` and cover:

- parser behavior and edge cases
- CVar conversion, bounds, and policy gating
- command execution and built-ins
- completion and history behavior
- persistence round-trips

## Notes

- If a setter has side effects, make it idempotent. `ApplyConsoleCVars()` is expected to run at a fixed frame sync point.
- Prefer module-scoped naming prefixes for symbols (for example `ngin.*`, `gfx.*`, `nput.*`, `cntt.*`).

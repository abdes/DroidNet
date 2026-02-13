# Oxygen Console and CVar Design

This document defines the `Oxygen.Console` module design and rollout plan.

## Goals

- Add a clean and pluggable runtime console subsystem.
- Allow modules to register CVars and commands without static-init hacks.
- Support command execution, result reporting, command history, and completion.
- Keep runtime behavior deterministic with latched/apply-at-sync semantics.

## Module Boundaries

- Host module: `src/Oxygen/Console`
- Consumer modules: `Engine`, `Renderer`, `Graphics`, `Input`, `Platform`, tools/editor layers.
- Ownership model:
  - `Oxygen.Console` owns registry, execution, history, and completion services.
  - Other modules own their own CVar/command definitions and register them in module init.

## Core Components

- `console::Console`: high-level façade used by engine/runtime.
- `console::Registry`: owns CVar and command registries and execution pipeline.
- `console::Parser`: tokenization (quotes/escapes/whitespace).
- `console::History`: ring-buffer with adjacent de-dup.
- `console::Completion`: currently implemented by `Registry::Complete`.

## Registration Model (Pluggable)

Each module registers in explicit setup code:

```cpp
void RendererModule::RegisterConsoleBindings(oxygen::console::Console& console)
{
  console.RegisterCVar({
    .name = "gfx.vsync",
    .help = "Enable VSync",
    .default_value = int64_t { 1 },
    .flags = CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 1.0,
  });
}
```

No registration via global constructors.

## CVar Semantics

### Minimal Semantics (must-have)

- Typed values: `bool`, `int64`, `double`, `string`.
- Name and help text.
- Default value and current value.
- Optional numeric min/max clamp for `int64` and `double`.
- Read-only enforcement.
- Query by entering only the CVar name.
- Set by entering `name value`.

### Typical Engine Semantics (target)

- `Archive`: persisted in JSON using Config `PathFinder::CVarsArchivePath()`.
- `ReadOnly`: runtime mutation forbidden (implemented).
- `Cheat`: blocked in shipping and restricted sources by policy matrix.
- `DevOnly`: blocked in shipping and restricted sources by policy matrix.
- `RequiresRestart`: mutation accepted and staged in `restart_value` until restart.
- `Latched`: changes staged and applied at sync point (`ApplyLatchedCVars`) (implemented).
- `RenderThreadSafe`: safe to read cross-thread with defined memory semantics (TODO).
- `Hidden`: excluded from `list`, `find`, `help` discovery, and completion.

## Command Semantics

- Command = `name + handler(args, context) -> ExecutionResult`.
- Execution context supports source and shipping-build policy checks.
- Remote execution requires explicit `RemoteAllowed`.
- Optional remote allowlist can further constrain remote commands by name.
- Optional audit hook receives source/subject/status/policy-denied events.
- Shipping can deny `Cheat`/`DevOnly` commands.
- Structured result: `status`, `exit_code`, `output`, `error`.

## History and Completion

- History:
  - Ring-like list with max capacity.
  - Adjacent duplicate suppression.
- Completion:
  - Prefix, case-insensitive over both command and CVar namespaces.
  - Ranked output candidates with kind/help metadata.
  - Ranking uses observed token usage frequency and recency.
  - Optional cycling state helper (`CompletionCycle`) for UI adapters.
- Built-ins:
  - `help [name]`, `find <pattern>`, `list [all|commands|cvars]`.
  - `exec <path>` executes script files line-by-line.
- Chaining:
  - `;` chains multiple commands in one input line.

Future target:

- Multi-provider completion ranking (exact prefix > fuzzy > recent usage).
- Tab-cycle stateful selection in UI layer.

## Integration Points

- Engine lifetime:
  - Construct `console::Console` early.
  - Register engine-owned bindings during `AsyncEngine` construction.
  - Register module bindings in `ModuleManager::RegisterModule` via
    `EngineModule::RegisterConsoleBindings`.
  - Apply latched CVars at deterministic `kFrameStart` boundary.
- UI:
  - ImGui panel should call `Execute`, `Complete`, and history navigation APIs.
  - UI must not directly mutate CVar internals.

### Startup/Frame Order (Implemented)

1. `AsyncEngine` constructs `console::Console`.
2. `AsyncEngine::RegisterConsoleBindings` registers engine-owned namespaces:
   - `ngin.*` (engine runtime)
   - `gfx.*` (graphics runtime)
   - `rndr.*` (renderer runtime)
   - `nput.*` (input runtime)
   - `cntt.*` (content/AssetLoader runtime)
3. `AsyncEngine::LoadPersistedConsoleCVars` loads persisted archive CVars.
4. `AsyncEngine::ApplyAllConsoleCVars` applies engine-owned and service/module
   owned CVars through inversion of control.
5. At each frame `kFrameStart`:
   - `ApplyConsoleStateAtFrameStart` runs `ApplyLatchedCVars` then
     `ApplyAllConsoleCVars`.

## Industry Patterns Captured

- Centralized registry with typed metadata.
- CVar flags for policy/persistence/latching.
- Deterministic apply points instead of arbitrary callback mutation.
- Console with structured execution results.
- History and completion as first-class features.

References used in research:

- Unreal console variables and commands:
  - <https://dev.epicgames.com/documentation/en-us/unreal-engine/console-variables-cplusplus-in-unreal-engine>
  - <https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-console-commands-reference>
- CryEngine console model:
  - <https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/29791867>
- ImGui console history/completion callback pattern:
  - <https://raw.githubusercontent.com/ocornut/imgui/master/imgui_demo.cpp>
- Linenoise history/completion baseline:
  - <https://raw.githubusercontent.com/antirez/linenoise/master/README.markdown>

# Console Implementation Tasks

This checklist is the tracking source for `Oxygen.Console`.

## Phase 0: Scaffolding

- [x] Create `src/Oxygen/Console` module and wire into `src/Oxygen/CMakeLists.txt`.
- [x] Add export header and public API surface (`Console`, `ConsoleTypes`, `Registry`).
- [x] Add a link/smoke test target under `src/Oxygen/Console/Test`.
- [x] Add design docs under `design/console`.

## Phase 1: Registry and Execution

- [x] Implement typed CVar registration (`bool`, `int64`, `double`, `string`).
- [x] Implement command registration with handler callback.
- [x] Implement command parser/tokenizer with quote and escape support.
- [x] Implement command execution with structured result type.
- [x] Implement CVar get/set execution path via console text.
- [x] Implement read-only CVar policy.
- [x] Implement latched CVar staging and explicit apply point.
- [x] Add unit tests for parser edge cases.
- [x] Add unit tests for CVar bounds and type conversion errors.
- [x] Add unit tests for command policy gating.

## Phase 2: Console UX Core

- [x] Implement in-memory history with adjacent dedupe.
- [x] Implement case-insensitive prefix completion for commands and CVars.
- [x] Add frequency/recency-ranked completion.
- [x] Add cycling completion state support for UI adapters.
- [x] Add `help`, `find`, and `list` built-in commands.
- [x] Add command chaining (`;`) and script file execution.

## Phase 3: Persistence and Policy

- [x] Persist `Archive` CVars to user config file.
- [x] Load CVar overrides from config and command line.
- [x] Implement `DevOnly` and `Cheat` policy for CVars.
- [x] Implement `RequiresRestart` CVar semantics.
- [x] Implement `Hidden` filtering in public listings/completion.
- [x] Add source-aware command policy matrix (`local`, `cfg`, `remote`, `automation`).
- [x] Add remote allowlist and audit logging hooks.

## Phase 4: Engine Integration

- [ ] Create `RegisterConsoleBindings` entry point per major module:
- [ ] `Renderer` (`rndr.*`)
- [ ] `Graphics` (`gfx.*`)
- [ ] `Engine` (`ngin.*`)
- [ ] `Input` (`nput.*`)
- [ ] `Scene` (`scn.*`)
- [ ] `Content` (`cntt.*`)
- [ ] Add deterministic `ApplyLatchedCVars` call at a known frame phase.
- [ ] Add startup registration order documentation.

## Phase 5: UI and Tooling

- [ ] Add ImGui console panel with:
- [ ] command line input
- [ ] history navigation
- [ ] completion popup
- [ ] output log view with severity coloring
- [ ] Add editor bridge API (if needed by `EditorInterface`).
- [ ] Add automation hooks for script-driven command execution.

## Definition of Done

- [ ] Module builds in all active presets.
- [ ] Tests cover parser, registry, policy, and completion behavior.
- [ ] Console commands/CVars are discoverable (`help`/`list`).
- [x] Archive persistence round-trip is validated.
- [x] Shipping policy blocks unsafe dev/cheat commands by default.

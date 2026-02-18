# Scripting Runtime Compilation & Caching Plan

Status: Implementation plan (locked decisions + execution tasks)
Scope: Runtime scripting execution path (scene load -> slot execution), compile/cache services, source resolution.

## 1. Locked Decisions

1. Module placement:
2. Compiler infrastructure and Luau backend live under `src/Oxygen/Scripting/*`.
3. Loader + asset integration stays under `src/Oxygen/Content/*`.
4. Service naming:
5. Use `ScriptCompilationService` as orchestrator.
6. Use `LuauScriptCompiler` (or equivalent) for Luau-specific compiler implementation.
7. Threading model:
8. Follow existing async model used by asset loading/coroutines.
9. Service uses async engine and transfers compilation work to worker threads from the thread pool.
10. Runtime behavior on compile failure:
11. Failure is reported once per slot.
12. Slot/script is then disabled.
13. No automatic retry.
14. Source policy:
15. External sources are enabled for now (full dev mode).
16. Supported source origins are all legal runtime origins: PAK, loose cooked, external file.
17. Source precedence (single authoritative source per asset):
18. Asset chooses embedded or external source path, not both as authoritative source.
19. If embedded contains bytecode+source: execution prefers embedded bytecode.
20. If bytecode unavailable in asset payload: try compiled-bytecode cache.
21. If cache miss: compile source and cache result.
22. Diagnostics:
23. Surface diagnostics in engine logs and scripting diagnostics channel (importer-style diagnostics design).
24. Script state must be exposed to runtime/consumers, including diagnostics.
25. Cache residency alignment:
26. Script assets and script resources are cacheable and pinned via AssetLoader dependency edges.
27. Eviction policy aligns with texture-style deferred eviction (`~1200` frames of non-use).
28. Ambiguity policy during implementation:
29. Stop and ask before making assumptions.

## 2. Goals

1. Scene loading must never block on script compilation.
2. Script execution before compilation completion must be deterministic no-op.
3. Compilation failures become visible only when async compilation completes.
4. Service design remains multi-thread-safe and deduplicates in-flight compilations.
5. Path resolution and policy enforcement must use `src/Oxygen/Config/PathFinder.h`.
6. Existing dependency-edge and cache residency behavior in AssetLoader must remain correct.

## 3. Non-Goals

1. Automatic file-watch hot reload (future feature).
2. Build-time hardening of external-source policy (future feature).
3. Blocking scene load until compilation completes.

## 4. Runtime Behavior Contract

1. Script slots are null-safe: `nullptr` executable means deterministic no-op.
2. Initial slot executable is `nullptr` (pending state).
3. Pending compilation executes as no-op.
4. Successful compilation atomically swaps slot executable to resolved executable.
5. Failed compilation keeps slot executable null-safe and disables the slot.
6. Run path remains non-throwing and non-crashing.
7. Compilation failure is emitted once per slot and script is disabled (no retry).
8. Slot exposes state and diagnostics:
9. `PendingCompilation`.
10. `Ready`.
11. `CompilationFailed`.

## 5. Architecture

### 5.1 `ScriptCompiler` and `LuauScriptCompiler` (Scripting module)

1. `ScriptCompiler` is language-agnostic compile interface.
2. `LuauScriptCompiler` implements Luau source->bytecode compilation.
3. Compiler-specific implementation remains backend-specific; service remains backend-agnostic.

Suggested API shape:

```cpp
struct ScriptCompileOptions {
  // optimization/debug/platform/runtime ABI knobs
};

struct ScriptCompileResult {
  bool success;
  std::vector<uint8_t> bytecode;
  std::string diagnostics;
  uint64_t compiler_fingerprint;
};

class ScriptCompiler {
public:
  virtual ~ScriptCompiler() = default;
  virtual auto Language() const noexcept -> data::pak::ScriptLanguage = 0;
  virtual auto Compile(std::span<const uint8_t> source,
    const ScriptCompileOptions& options) -> ScriptCompileResult = 0;
};
```

### 5.2 `ScriptCompilationService` (orchestrator)

1. Lives at module boundary where runtime scripting + content loading meet.
2. Responsibilities:
3. Resolve source blob.
4. Compute compile key.
5. Query two-level cache.
6. Deduplicate in-flight compile requests.
7. Dispatch to language-specific compiler.
8. Publish result to subscribers/slots.
9. Non-blocking enqueue API; never blocks scene load path.

### 5.3 `ScriptSourceResolver` with `PathFinder`

1. Produces normalized `ScriptSourceBlob` from supported origins.
2. Uses `PathFinder` for canonicalization and allowed-root policy.
3. Supports:
4. PAK source resource.
5. Loose cooked source.
6. External source file.

Suggested model:

```cpp
enum class ScriptSourceOrigin { Pak, LooseCooked, ExternalFile };

struct ScriptSourceBlob {
  std::vector<uint8_t> bytes;
  ScriptSourceOrigin origin;
  std::string origin_id;      // asset key and/or canonical path identity
  std::string canonical_path; // when path-backed
};
```

### 5.4 Slot executable indirection

1. Slot stores atomically replaceable `std::shared_ptr<const IScriptExecutable>`.
2. Run path always dispatches through executable interface.
3. No run-path branching on compile status.

```cpp
class IScriptExecutable {
public:
  virtual ~IScriptExecutable() = default;
  virtual auto Run(const ScriptRunContext& ctx) noexcept -> void = 0;
};
```

Implementations:

1. `CompiledScriptExecutable`.
2. `FailedScriptExecutable` (disabled slot behavior + diagnostics integration).

## 6. Caching Design

### 6.1 Compile key

Compile key includes:

1. Source bytes hash.
2. Script identity (asset key or equivalent stable identity).
3. Script language.
4. Compiler fingerprint/version.
5. VM bytecode version.
6. Compile options.
7. Target platform/runtime ABI salt.

### 6.2 Two-level cache

1. Memory cache: LRU map keyed by compile key.
2. Persistent cache: `scripts.bin` family located beside shader cache artifact (`shaders.bin` location policy).

No ocean boiling choice:

1. Start with a single append-friendly binary file + compact in-memory index loaded at startup.
2. Keep format versioned and keyed by compile key.
3. Defer DB/chunked storage complexity unless required by scale.

### 6.3 In-flight dedupe

1. `in_flight[key] -> shared task/future`.
2. First requester creates compile task.
3. Others attach to same task.
4. Completion publishes once then removes in-flight entry.

## 7. Source Selection and Execution Priority

1. Asset declares authoritative source mode (embedded or external path).
2. Embedded mode:
3. If embedded bytecode exists: use it first.
4. Else try compiled-bytecode cache.
5. Else compile from embedded source.
6. External mode:
7. Resolve via `PathFinder`.
8. Try compiled-bytecode cache.
9. Else compile external source.
10. Loose cooked source is legal and resolved through same resolver path for editor/import workflows.

## 8. AssetLoader and Residency Alignment

1. Keep baseline pinning for script assets.
2. Keep script resources in cache as shared resources with dependency edges.
3. Scene dependency collector must emit script asset dependencies from scripting slot records.
4. Script compilation work must respect pinned asset/resource lifetimes while in use.
5. Eviction behavior remains unified with existing deferred policy (`~1200` frames non-use).

## 9. Concurrency and Safety

1. Protect service maps (`cache`, `in_flight`, subscribers) with explicit synchronization.
2. Use atomic shared_ptr load/store for slot executable handle.
3. Script run path is noexcept and non-throwing.
4. Failure notification is once-per-slot for compilation failures.

## 10. Phased Task Plan

### Phase A: Interfaces and slot-state runtime contract

Status: DONE.

1. Add `IScriptExecutable` and concrete implementations.
2. Add slot state enum (`PendingCompilation`, `Ready`, `CompilationFailed`) and diagnostics attachment.
3. Update scripting runtime to always call executable indirection.
4. Ensure compile failure disables slot and emits once-per-slot diagnostics.

### Phase B: compiler infra in Scripting module

Status: DONE.

1. Add `ScriptCompiler` interface.
2. Add `LuauScriptCompiler` implementation.
3. Add compiler registry keyed by script language.

### Phase C: service orchestration

Status: DONE.

1. Add `ScriptCompilationService`.
2. Implement async queueing + worker handoff.
3. Implement in-flight dedupe.
4. Implement result publication to slot subscribers.

### Phase D: source resolution + PathFinder

Status: DONE.

1. Add `ScriptSourceResolver`.
2. Implement PAK source path.
3. Implement loose cooked source path.
4. Implement external source path (enabled by current dev policy).
5. Normalize to `ScriptSourceBlob` for service pipeline.

### Phase E: executable and caching pipeline

#### E1. Executable contract split

Status: DONE.

1. Move `ScriptExecutable` interface out of Scene into core contract surface.
2. Keep Scene depending only on interface handles.
3. Keep concrete executable types in Scripting (`Failed`, compiled runtime executable wrappers).
4. Preserve runtime behavior (`PendingCompilation`, `Ready`, `CompilationFailed`) with no blocking.

Checkpoint E1:

1. Build Scene + Scripting targets.
2. Existing Scene scripting tests pass with no behavior regression.

#### E2. Hydration Integration

##### E2.1 Scene build phase refactor (DemoShell / RenderScene)

Status: DONE.

1. Move heavy scene construction/hydration out of `OnFrameStart`.
   Status: DONE.
2. Keep `OnFrameStart` limited to atomic scene publish only.
   Status: DONE.
3. Preserve existing single-ownership model through `DemoShell::SetScene(std::unique_ptr<scene::Scene>)`.
   Status: DONE.
4. Build next scene in mutation/coroutine phase, then publish in frame-start (all-or-nothing full-frame visibility).
   Status: DONE.
5. Ensure no partial scene state is ever observable by later phases in the same frame.
   Status: DONE.

Checkpoint E2.1:

1. Scene build/hydration no longer executes in `OnFrameStart`.
   Status: DONE.
2. Scene ownership/swap path remains unchanged (`DemoShell::SetScene(...)`).
   Status: DONE.
3. Frame-level scene visibility invariant is preserved.
   Status: DONE.

##### E2.2 Script hydration + compilation service wiring

Status: DONE.

1. Define and lock the responsibility boundary for runtime script hydration:
2. `Content` provides loaded `SceneAsset` + `ScriptAsset` availability only.
   Status: DONE.
3. `SceneLoaderService` owns runtime node scripting-component hydration only.
   Status: DONE.
4. `Scripting` service owns compile orchestration only.
   Status: DONE.
5. Explicitly do not introduce a generic "binding layer" abstraction.
   Status: DONE.
6. Explicitly do not expose raw PAK slot-table offset/range mechanics to DemoShell-facing hydration code.
   Status: DONE.

7. Add clean `IAssetLoader` script-asset API parity:
8. `StartLoadScriptAsset(...)`, `GetScriptAsset(...)`, `HasScriptAsset(...)`.
   Status: DONE.
9. In scene hydration, attach scripting slots from scene components + script assets directly (without any intermediate binding subsystem).
   Status: DONE.
10. Use engine-owned compilation service handle from `AsyncEngine` to request async slot compilation from hydration (replacing old module weak-handle design).
    Status: DONE.
11. Keep null-safe slot contract:
12. Immediate slot remains executable-null (pending/no-op).
    Status: DONE.
13. Completion callback updates slot via `MarkSlotReady(...)` or `MarkSlotCompilationFailed(...)`.
    Status: DONE.
14. Failure remains once-per-slot and disables slot (no retry).
    Status: DONE.

Checkpoint E2.2:

1. Scene hydration creates scripting slots with pending state and no-op-safe execution.
   Status: DONE.
2. Successful async compilation transitions slots to ready via callback path.
   Status: DONE.
3. Failed async compilation transitions slots to disabled/failure once.
   Status: DONE.
4. New script-asset loader APIs are used by hydration without layering leaks.
   Status: DONE.
5. DemoShell-side hydration code has zero knowledge of raw PAK slot-table offsets/ranges.
   Status: DONE.
6. No generic binding abstraction is introduced for scripting hydration.
   Status: DONE.

##### E2.3 Engine-owned compile-service boundary and lifecycle

Status: DONE.

1. Define source-agnostic compile-service contracts in an Engine-owned source module (or Engine service layer), not in `src/Oxygen/Scripting/*`.
2. Move only language-neutral types/contracts out of Scripting:
3. compile request/result DTOs.
4. subscriber/completion callback contracts.
5. service control surface (`AcquireForSlot`, `OnFrameStart`, shutdown/stop hooks).
6. Keep language/runtime-specific implementation in Scripting:
7. compiler backends (Luau).
8. source resolver implementations.
9. executable/runtime glue.
10. Enforce dependency direction:
11. Engine must not include or link against Scripting module internals.
12. Scripting depends on Engine contracts, never the reverse.

13. Refactor `ScriptingModule` to consume an Engine-owned compilation service handle only.
14. Remove compilation-service lifecycle ownership from `ScriptingModule` (no activation of service in module attach).
15. Keep module responsibilities limited to VM/runtime phase behavior and execution policy.

16. Make `ScriptCompilationService` a proper `co::LiveObject`.
17. Activation/deactivation originates from `AsyncEngine` master nursery lineage only.
18. `AsyncEngine` calls service frame-start dispatch before module frame phases.
19. Service owns compile kickoff/in-flight task lifetime internally.
20. `SceneLoaderService` keeps callback wiring only and does not own compile task kickoff.

Checkpoint E2.3:

1. Engine compiles with zero dependency from Engine to Scripting source module.
   Status: DONE.
2. `ScriptingModule` no longer activates or owns compilation-service runtime lifecycle.
   Status: DONE.
3. `ScriptCompilationService` is activated/stopped by `AsyncEngine` as a `co::LiveObject`.
   Status: DONE.
4. Compile completion dispatch is driven by engine service frame-start path.
   Status: DONE.
5. Scene hydration path uses service callbacks without any loader-side coroutine nursery ownership.
   Status: DONE.

E2 remaining work summary:

1. None.

#### E3. L1 executable cache (in-process)

Status: DONE.

1. Add Scripting L1 cache for compiled bytecode blobs keyed by compile key.
2. Integrate with existing in-flight dedupe.
3. Reuse immutable bytecode artifacts across slots/scenes when key matches.
4. Keep per-slot executable handles as runtime wrappers; do not cache slot-scoped executable instances in L1.

Checkpoint E3:

1. L1 hit avoids recompilation in-process.
2. Concurrent same-key requests compile once.
3. Reuse path is deterministic across multiple slots via shared bytecode.

#### E4. L2 persistent cache (`scripts.bin`)

Status: DONE.

1. Implement single-file, versioned cache format.
2. Layout: header + index + payload blocks (bytecode only; no runtime execution state).
3. Load index at startup; lazy-read payload on hit.
4. Add atomic write/update path and corruption fallback (warn + miss).

Checkpoint E4:

1. Roundtrip persistence test passes across restart.
   Status: DONE.
2. Version mismatch invalidation works.
   Status: DONE.
3. Corruption recovery falls back cleanly to compile path.
   Status: DONE.

#### E5. End-to-end cache pipeline wiring

Status: DONE.

1. Wire lookup order:
2. AssetLoader payload -> Scripting L1 -> Scripting L2 -> async compile -> L2 store -> L1 publish.
3. Use full compile key invalidation dimensions:
4. Source hash, script identity salt, language, compile mode, compiler fingerprint, VM bytecode version, platform/ABI salt.
5. Publish resolved executable through E2 completion mechanism.

Checkpoint E5:

1. Embedded bytecode path bypasses source compilation.
   Status: DONE.
2. Warm L2 path avoids compilation.
   Status: DONE.
3. Cold path compiles once and warms both caches.
   Status: DONE.
4. Scene loading remains non-blocking.
   Status: DONE.

#### E6. Observability and guardrails

Status: DONE.

1. Add logs for hit/miss/compile/store/publish transitions.
2. Add counters for L1 hit, L2 hit, compile count, failure count.
3. Finalize docs for executable ownership, slot update contract, and cache invariants.

Checkpoint E6:

1. Diagnostics/counters validated in focused tests.
   Status: DONE.
2. Manual end-to-end run confirms expected state transitions and logs.
   Status: DONE.

### Phase F: Content/AssetLoader integration

Status: DONE.

1. Keep `AssetLoader` content-only (load/decode/cache/dependency/residency).
   Script compilation enqueue is triggered from scene hydration via
   `ScriptCompilationService`, without blocking scene load.
   Status: DONE.
2. Re-verify script dependency edges emission from scene scripting slots.
   Status: DONE.
3. Re-verify asset/resource residency and eviction behavior.
   Status: DONE.

Checkpoint F:

1. `AssetLoader` does not compile scripts and does not own compile kickoff.
   Status: DONE.
2. Scene scripting slots emit script asset dependencies, and script assets emit
   script resource dependencies.
   Status: DONE.
3. Script assets/resources remain pinned and follow existing deferred eviction
   behavior through dependency edges.
   Status: DONE.

### Phase G: diagnostics and telemetry

Status: DONE (completed as part of E6).

1. Integrate scripting diagnostics channel output for compile failures.
2. Add compile counters and latency metrics.
3. Add structured logs with source origin + asset identity + compile key.

## 11. Verification Checklist

### 11.1 Functional

1. Scene load succeeds without waiting for compilation.
2. Pending scripts execute as no-op.
3. Successful compile activates script execution without scene reload.
4. Compile failure marks slot disabled and emits one diagnostic per slot.
5. Embedded bytecode path executes without source compilation.
6. Embedded source path compiles + caches + executes.
7. Loose cooked source compiles + caches + executes.
8. External source compiles + caches + executes.

### 11.2 Cache and dedupe

1. Concurrent requests for same compile key produce single compile task.
2. Memory cache hit avoids recompilation in-process.
3. Persistent cache hit avoids recompilation across restarts.
4. Compiler/version/options changes invalidate stale entries.

### 11.3 Integration

1. AssetLoader script dependency edges remain correct.
2. Script assets/resources remain pinned/evicted under existing policy.
3. No regression in non-scripting asset loading paths.

### 11.4 Diagnostics/state

1. Slot state transitions are observable and correct.
2. Diagnostics payload is surfaced via engine logs + scripting diagnostics channel.
3. Disabled slots remain disabled after compile failure (no silent retry).

## 12. Test Plan

1. `ScriptCompilationService_test`.
2. `ScriptSourceResolver_test`.
3. `LuauScriptCompiler_test`.
4. `ScriptingComponent_compile_state_test`.
5. `ScriptingComponent_noop_placeholder_test`.
6. `ScriptingComponent_failure_disables_slot_test`.
7. `AssetLoader_script_residency_test`.
8. Focused `SceneLoader_scripting_unit_test` cases (small and isolated, avoid bloating full scene tests).

## 13. Future List

1. Automatic file-watch/hot-reload for loose/external sources.
2. External-source policy hardening for non-dev builds.
3. Alternative persistent-cache backends if `scripts.bin` model becomes insufficient.

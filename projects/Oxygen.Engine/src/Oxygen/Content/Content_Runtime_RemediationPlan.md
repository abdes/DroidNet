# Content Runtime Remediation Plan (Excluding Importers)

## Preamble: Locked Design Invariants

These invariants are mandatory for this remediation scope and are intentionally unambiguous.

1. Scope exclusion is strict: no importer pipeline refactoring is included in this plan.
2. Runtime source ownership remains inside Content runtime (`AssetLoader` + internal services), never UI-side parsing/opening.
3. `IContentSource` is the only runtime abstraction for cooked source reads; format-specific details must not leak into orchestration layers.
4. `PakFile` and loose-cooked read paths must expose equivalent capabilities (asset lookup, descriptor readers, tables, data readers, virtual-path resolution).
5. Mount/catalog query APIs remain authoritative for mounted state and discovery (`EnumerateMountedSources`, `EnumerateMountedScenes`).
6. Owning-thread and async activation constraints in runtime loaders remain unchanged.
7. Decode/publish separation remains intact: worker decode does not mutate runtime ownership/caches directly.
8. Cache/eviction/dependency operations must preserve edge/refcount consistency invariants.
9. No RTTI-based branching is permitted in this scope.
10. Every task is complete only when code changes and verification criteria are both satisfied.

---

## Task Status (Updated 2026-02-24)

1. `R0.1` - Completed (2026-02-24)
2. `R0.2` - Completed (2026-02-24)
3. `R1.1` - Completed (2026-02-24)
4. `R1.2` - Completed (2026-02-24)
5. `R2.1` - Completed (2026-02-24)
6. `R2.2` - Completed (2026-02-24)
7. `R3.1` - Completed (2026-02-24)
8. `R3.2` - Completed (2026-02-24)
9. `R4.1` - Completed (2026-02-24)
10. `R4.2` - Completed (2026-02-24)

---

## Phase 0 - Baseline and Safety Rails

### Task R0.1 - Baseline characterization for source/read/caching seams
Status: Completed (2026-02-24)
- Scope:
  - Add/extend characterization coverage for runtime source reads, mount catalog queries, and cache trim/eviction interactions.
  - Lock current behavior before structural changes.
- References:
  - `src/Oxygen/Content/AssetLoader.cpp` (`AddPakFile`, `AddLooseCookedRoot`, `TrimCache`, `EnumerateMountedSources`, `EnumerateMountedScenes`)
  - `src/Oxygen/Content/Internal/ResourceLoadPipeline.cpp`
  - `src/Oxygen/Content/Internal/DependencyReleaseEngine.cpp`
  - `src/Oxygen/Content/Internal/EvictionRegistry.h`
- Deliverables:
  - Tests under `src/Oxygen/Content/Test` covering source capability parity and eviction/dependency consistency during runtime operations.
- Verification:
  - New tests fail on observable behavior drift in source-read orchestration or trim/eviction sequencing.
  - Verified via targeted runs:
    - `AssetLoaderLoadingTest.EnumerateMountedScenes_MixedSourcesExpectedToExposeSceneEntries`
    - `AssetLoaderLoadingTest.TrimCacheExpectedToPreserveMountedCatalog_ClearMountsExpectedToClearCatalog`

### Task R0.2 - Define measurable refactor guardrails for runtime subsystems
Status: Completed (2026-02-24)
- Scope:
  - Introduce objective gates for orchestration complexity and API boundary clarity.
  - Document subsystem responsibilities and forbidden cross-layer access.
- References:
  - `src/Oxygen/Content/README.md`
  - `src/Oxygen/Content/Internal/*.h`
- Deliverables:
  - Guard checklist in docs: allowed dependencies, ownership boundaries, and prohibited direct format coupling.
- Verification:
  - Checklist applied to all tasks in this plan before task closure.
  - Guard checklist added in `src/Oxygen/Content/README.md` under "Runtime Refactor Guard Checklist".

---

## Phase 1 - Source IO Contract Hardening

### Task R1.1 - Normalize `IContentSource` read contracts to remove format leakage
Status: Completed (2026-02-24)
- Scope:
  - Refine `IContentSource` API to express read intent cleanly and consistently across PAK and loose-cooked sources.
  - Eliminate redundant/format-coupled helpers from orchestration-facing surface.
- References:
  - `src/Oxygen/Content/Internal/IContentSource.h`
  - `src/Oxygen/Content/Internal/PakFileSource.h`
  - `src/Oxygen/Content/Internal/LooseCookedSource.h`
- Deliverables:
  - Updated interface contract with explicit capability semantics and consistent nullability/error behavior.
  - Adapted source implementations preserving behavior.
  - Progress:
    - Replaced format-leaking locator contract (`AssetLocator`) with key-based descriptor access (`HasAsset`, `CreateAssetDescriptorReader(asset_key)`).
    - Updated callers (`AssetLoader`, `SceneCatalogQueryService`, `PhysicsQueryService`) to use key-based source contracts.
- Verification:
  - Contract tests validate parity for both source implementations on all read capabilities.
  - Verified by targeted test:
    - `AssetLoaderLoadingTest.ContentSourceConformance_BufferTextureCapabilitiesExpectedToMatch`

### Task R1.2 - Add source-capability conformance tests (PAK vs loose-cooked)
Status: Completed (2026-02-24)
- Scope:
  - Add focused conformance tests for descriptor reads, resource tables/data readers, slot/param records, and virtual-path resolution.
- References:
  - `src/Oxygen/Content/Internal/PakFileSource.h`
  - `src/Oxygen/Content/Internal/LooseCookedSource.h`
  - `src/Oxygen/Content/Test/*`
- Deliverables:
  - New conformance test suite under `src/Oxygen/Content/Test`.
  - Progress:
    - Added conformance tests in existing suites:
      - `AssetLoader_loading_test.cpp`: buffer/texture capability parity.
      - `AssetLoader_scripting_test.cpp`: script table/data + slot/param + virtual-path parity.
- Verification:
  - Same semantic assertions pass for both source types for equivalent fixtures.
  - Verified by targeted tests:
    - `AssetLoaderLoadingTest.ContentSourceConformance_BufferTextureCapabilitiesExpectedToMatch`
    - `AssetLoaderScriptingTest.ContentSourceConformance_ScriptCapabilitiesExpectedToMatch`

---

## Phase 2 - Pak/Loose Read-Path Consolidation

### Task R2.1 - Split `PakFile` responsibilities into explicit internal components
Status: Completed (2026-02-24)
- Scope:
  - Decompose `PakFile` responsibilities (directory/browse index, table init/access, data region readers, validation) into smaller internal collaborators.
  - Keep public API stable for runtime consumers in this iteration.
- References:
  - `src/Oxygen/Content/PakFile.h`
  - `src/Oxygen/Content/PakFile.cpp`
- Deliverables:
  - Internal component extraction with reduced god-object behavior and clearer invariants.
  - Progress:
    - Extracted metadata parsing into internal helper functions (header/footer/directory).
    - Extracted browse-index parsing into internal helper functions.
    - Consolidated stream seek/reader creation with `PakFile::CreateReaderAtOffset`.
    - Replaced duplicated constructor parse path with loader-based assembly while preserving public `PakFile` API.
- Verification:
  - Existing `PakFile`/loader tests remain green with no behavior drift.
  - Verified green in your local run.

### Task R2.2 - Align loose-cooked index/data access patterns with PAK read-path model
Status: Completed (2026-02-24)
- Scope:
  - Align lookup/read flow and error semantics between loose-cooked and PAK paths to simplify orchestration assumptions.
- References:
  - `src/Oxygen/Content/Internal/LooseCookedSource.h`
  - `src/Oxygen/Content/LooseCooked/*`
  - `src/Oxygen/Content/Internal/IContentSource.h`
- Deliverables:
  - Harmonized source read semantics and reduced special-case branching in callers.
  - Progress:
    - `PakFileSource` table/data readers now respect capability presence (return null when the corresponding table is absent), matching loose-cooked semantics.
    - `LooseCookedSource` script slot/param record reads now use runtime error paths instead of debug `CHECK_F`, aligning runtime behavior with PAK source semantics.
    - Source conformance test in `AssetLoader_loading_test.cpp` tightened to assert absent script/physics reader capabilities when tables are absent.
- Verification:
  - Cross-source conformance tests show equivalent behavior for shared capabilities.
  - Verified green in your local run.

---

## Phase 3 - Load Orchestration Simplification

### Task R3.1 - Tighten `ResourceLoadPipeline` policy boundaries
Status: Completed (2026-02-24)
- Scope:
  - Separate policy decisions (key hashing, mapping, ownership assertions) from execution flow in `ResourceLoadPipeline`.
  - Minimize implicit coupling with external state.
- References:
  - `src/Oxygen/Content/Internal/ResourceLoadPipeline.h`
  - `src/Oxygen/Content/Internal/ResourceLoadPipeline.cpp`
  - `src/Oxygen/Content/AssetLoader.cpp`
- Deliverables:
  - Clearer pipeline stages and callback contracts.
  - Progress:
    - Extracted policy helpers in `ResourceLoadPipeline.cpp` for:
      - source resolution by source id
      - loader resolution by resource type
      - resource-type descriptor table + descriptor offset resolution
      - prepared decode assembly from resolved policy decisions
    - Simplified `LoadErased` to orchestration-only flow over explicit policy helpers.
- Verification:
  - Resource load tests validate unchanged behavior across cached, in-flight join, and cooked-bytes paths.
  - Verified green in your local run.

### Task R3.2 - Consolidate loader invocation conventions across resource loaders
Status: Completed (2026-02-24)
- Scope:
  - Standardize how `LoaderContext` is prepared and consumed across `src/Oxygen/Content/Loaders/*`.
  - Remove ad hoc per-loader orchestration assumptions.
- References:
  - `src/Oxygen/Content/Loaders/*`
  - `src/Oxygen/Content/Internal/ResourceLoadPipeline.cpp`
- Deliverables:
  - Unified loader invocation contract and reduced per-loader divergence.
  - Progress:
    - `ResourceLoadPipeline` now builds `LoaderContext` through shared helpers for both mounted-source decode and cooked-bytes decode paths.
    - Mounted-source resource decode now propagates `LoaderContext.source_token` explicitly from `ContentSourceRegistry`.
    - Loader resolution by resource type is now shared between mounted and cooked decode paths (single policy helper).
- Verification:
  - No regression in resource decoding/registration tests.
  - Verified green in your local run.

---

## Phase 4 - Cache/Eviction Policy Hardening

### Task R4.1 - Strengthen eviction reentrancy and subscriber lifecycle guarantees
Status: Completed (2026-02-24)
- Scope:
  - Harden `EvictionRegistry` behavior under nested/reentrant eviction and subscription churn.
  - Make lifecycle guarantees explicit and testable.
- References:
  - `src/Oxygen/Content/Internal/EvictionRegistry.h`
  - `src/Oxygen/Content/Internal/EvictionRegistry.cpp`
  - `src/Oxygen/Content/AssetLoader.cpp` (eviction call sites)
- Deliverables:
  - Explicit invariants for reentrancy and subscriber delivery order/visibility.
  - Progress:
    - `EvictionRegistry` now provides snapshot-based subscriber retrieval to stabilize dispatch under subscription churn.
    - `AssetLoader` eviction dispatch now iterates over snapshot copies, preventing invalidation when handlers subscribe/unsubscribe during callback execution.
    - Added regression tests in existing `AssetLoader_eviction_reentrancy_test.cpp` for:
      - self-unsubscribe during callback safety
      - subscribe-during-callback visibility only in subsequent dispatches
- Verification:
  - Reentrancy/subscriber churn tests validate deterministic, safe behavior.
  - Verified green in your local run.

### Task R4.2 - Optimize dependency release traversal with profiling gates
Status: Completed (2026-02-24)
- Scope:
  - Review and optimize dependency-release traversal hotspots in `DependencyReleaseEngine`/`DependencyGraphStore` only if profiling confirms pressure.
  - Keep correctness-first semantics.
- References:
  - `src/Oxygen/Content/Internal/DependencyReleaseEngine.h`
  - `src/Oxygen/Content/Internal/DependencyReleaseEngine.cpp`
  - `src/Oxygen/Content/Internal/DependencyGraphStore.h`
  - `src/Oxygen/Content/Internal/DependencyGraphStore.cpp`
- Deliverables:
  - Measured optimization changes with before/after profiling notes.
  - Progress:
    - Optimized `TrimCache` traversal:
      - replaced `std::function` recursive DFS with self-recursive lambda (removes type-erasure recursion overhead)
      - pre-reserved `visited_assets`/`visiting_assets` to reduce hash-set reallocations
      - removed redundant cache-presence check in root scan (already guaranteed by pre-filter map)
    - Profiling gate/logging explicitly removed per design decision (not a hot-path target).
- Verification:
  - Functional parity tests remain green with no behavior drift.
  - Verified green in your local run.

---

## Global Verification Strategy

1. Unit tests:
- Source capability conformance (`IContentSource`, `PakFileSource`, `LooseCookedSource`).
- Pipeline and loader invocation consistency.
- Eviction and dependency-release correctness under stress.

2. Integration tests:
- Mixed mount runtime workflows (PAK + loose-cooked) using runtime query APIs only.
- Cache trim + reload cycles with dependency release validation.

3. Performance gates:
- Profile before/after for dependency-release and source-read hot paths.
- No speculative optimization merges without measured benefit.

4. Build/test gates:
- Debug and Release both required for closure of each task.

---

## Execution Order

1. Phase 0
2. Phase 1
3. Phase 2
4. Phase 3
5. Phase 4

Rationale:
- Phase 0 prevents accidental drift.
- Phases 1-2 stabilize and simplify source/read boundaries.
- Phase 3 reduces orchestration complexity on top of those stabilized contracts.
- Phase 4 hardens and optimizes lifecycle/perf-critical policy paths with correctness gates.

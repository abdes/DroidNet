<!-- Canonical implementation status & planning hub for Oxygen Content subsystem -->

# Content Subsystem Implementation Plan

**Last updated:** August 2025

## Quick Links

| Section | Anchor |
|---------|--------|
| Current Status Summary | #current-status-summary |
| Detailed Feature Matrix | #detailed-feature-matrix |
| Roadmap Phases | #roadmap-phases |
| Backlog / Open Items | #backlog--open-items |
| Glossary | #glossary |

---

## Current Status Summary

Synchronous loading, caching, and dependency registration are implemented. Safe unloading is partial (ref counts + dependency registration exist; strict protection for depended-on assets still WIP). Async pipeline, streaming, and advanced eviction/hot-reload remain planned.

---

## Detailed Feature Matrix

### Core Infrastructure

| Feature | Status | Notes |
|---------|--------|-------|
| PAK File Format | ✅ Complete | Binary container with alignment & directory |
| Asset Directory | ✅ Complete | Key → descriptor mapping |
| Resource Tables | ✅ Complete | Type-safe indexed access |
| Asset Key System | ✅ Complete | 16-byte key (GUID+metadata) |
| Asset Type System | ✅ Complete | Extensible enum |

### Asset Loading Pipeline

| Feature | Status | Notes |
|---------|--------|-------|
| Synchronous Asset Loader | ✅ Complete | LoaderContext pattern |
| Loader Registration | ✅ Complete | Unified registration API |
| Resource Caching | ✅ Complete | ResourceTable + ref counting |
| Dependency Registration | ✅ Complete | Asset→Asset & Asset→Resource inline |
| Safe Asset Unloading | 🔄 Partial | Need full dependent validation + cascade guarantees |
| Asset Caching | ✅ Complete | Asset cache with ref counts |
| Hot Reload | ❌ Missing | No invalidation or watcher |

### Asynchronous System (Design Stage)

| Feature | Status | Notes |
|---------|--------|-------|
| Coroutine-based API | ❌ Missing | Design in `asset_loader.md` |
| ThreadPool Integration | 🔄 Partial | Primitive exists; not wired to loaders |
| Async File I/O | ❌ Missing | Blocking reads currently |
| GPU Upload Queue | ❌ Missing | Awaitable design only |
| Background Processing | ❌ Missing | CPU offload not implemented |

### Asset Types

| Type | Status | Notes |
|------|--------|-------|
| GeometryAsset | ✅ Complete | Multi-LOD + submesh hierarchy |
| BufferResource | ✅ Complete | Vertex / index / constant buffers |
| TextureResource | ✅ Complete | 2D / array / cube |
| MaterialAsset | ✅ Complete | Shader + texture refs |
| SceneAsset | ❌ Missing | Scene composition |
| AnimationAsset | ❌ Missing | Anim sequences |
| AudioResource | ❌ Missing | Compressed audio |

### Streaming & Chunking

| Feature | Status | Notes |
|---------|--------|-------|
| Chunked Loading | ❌ Missing | Documented in `chunking.md` |
| Memory Mapping | ❌ Missing | Potential zero-copy future |
| GPU Alignment | ✅ Complete | 256B / 16B / 4B rules implemented |
| Progressive Loading | ❌ Missing | Priority-based scheduling |
| Residency Management | ❌ Missing | Budget + eviction policies |

### Tooling & Analysis

| Tool | Status | Notes |
|------|--------|-------|
| PAK Generator | ✅ Complete | YAML → binary |
| PAK Dumper | ✅ Complete | Introspection / debug |
| Performance Profiler | ❌ Missing | Load timing & memory stats |
| Dependency Analyzer | ❌ Missing | Graph & shared usage inspection |

### Testing & Validation

| Area | Status | Notes |
|------|--------|-------|
| Unit Tests | ✅ Excellent | Coverage for loaders & cache logic |
| Integration Tests | ✅ Good | Directory + dependency flows |
| Performance Tests | ❌ Missing | Need benchmarks |
| Memory Tests | ❌ Missing | Leak & fragmentation tracking |

---

## Roadmap Phases

### Phase 1 (Foundation) – Mostly Complete

Focus: caching, dependency registration, reference counting, baseline loaders.

Remaining: strict dependent-protection on unload (finalize reverse maps &
validation), clearer unloader contract docs.

#### Safe Asset Unloading – Action Plan (Reverse Maps Removed)

Decision: Remove `reverse_asset_dependencies_` and
`reverse_resource_dependencies_`. Lifetime safety is already enforced by
`AnyCache` reference counts. Reverse maps add maintenance & memory cost without
contributing to correctness.

Rationale:

* Correctness: Eviction is already prevented while any checkout (direct or
  dependency Touch) exists.
* Simplicity: Fewer structures to mutate/prune; reduces risk of drift.
* Memory: Avoids unbounded accumulation of never-pruned dependent sets.
* Diagnostics: Future tooling will perform on-demand forward scans or expose
  cache checkout counts; no need for constantly-updated reverse indices.

Execution Steps (Phase 1 – assumes full C++20 availability, no need to support
pre-C++20 compilers):

1. Delete reverse map members from `AssetLoader.h` and their insertion lines in
   `AddAssetDependency` / `AddResourceDependency`.
2. Add `ForEachDependent(const AssetKey&, Fn)` (debug-only) that linearly scans
   `asset_dependencies_` to enumerate direct dependents when needed (tests /
   diagnostics).
3. Implement cycle detection in `AddAssetDependency` (DFS over forward edges)
   rejecting edges that introduce a cycle (log + assert in debug, no insertion).
4. Add visited guard (debug-only) in `ReleaseAssetTree` asserting no re-entry.
   Implementation detail: maintain a small `thread_local`
   `flat_set`/`unordered_set` (cleared on outermost call) of active asset keys.
   On entry: if key already present -> `DCHECK_F(false)` (cycle) and early
   return in release builds to avoid infinite recursion. This is defensive;
   primary prevention is compile-time cycle rejection in step 3. Use RAII helper
   to insert/erase ensuring exception safety.
5. Expand header docs to explicitly specify unload ordering: resources first,
   then asset dependencies, then asset; unloader must not trigger new loads
   (warn if it does in debug by checking cache checkout delta).
6. Document and enforce single-threaded use for Phase 1 (store constructing
   thread id; `DCHECK_F` on public mutating calls). Concurrency redesign
   deferred to async phase.
7. Verbose eviction diagnostics using existing logging levels (e.g. `LOG_F(2,
   ...)`) inside eviction callback—no compile-time flags; rely on runtime log
   verbosity configuration.
8. Add tests (Phase 1 closure criteria):

* CycleDetection_PreventsInsertion
* CascadeRelease_SiblingSharedDependencyNotEvicted
* ReleaseOrder_ResourcesBeforeAssets (instrument unload order)
* DebugDependentEnumeration_Works (non-release build)
* EvictionDiagnostics_CompileTimeGuarded (flag toggles logging)

Acceptance (Safe Unloading COMPLETE): reverse maps removed; cycle detection +
recursion guard active; unloader contract documented & verified; single-thread
policy enforced; new tests green; no memory growth from stale dependents in
stress test; verbose diagnostics emitted only when log level >=2.

### Phase 2 (Async Pipeline)

1. Introduce coroutine task wrappers around current sync loads.
2. Integrate ThreadPool using `co_threadpool` for decode / packing.
3. Implement GPU Upload Queue (thread + fence awaitable).
4. Provide dual API (Sync fallback → Async core) for migration.
5. Add minimal cancellation primitive (token passed through context).

### Phase 3 (Advanced Features)

1. Hot Reload (file hash watch + invalidation queue).
2. Streaming / Chunked & prioritized loads (distance / importance scoring).
3. Memory Residency & Budgets (GPU heap usage + soft limits).
4. Eviction policies (LRU + size + custom callbacks).
5. Dependency Analyzer Tool (graph export, hotspot report).

### Phase 4 (Quality & Observability)

1. Performance benchmarks (cold load, warm cache, parallel scenario).
2. Telemetry integration (per-stage timing, histogram).
3. Diagnostic validation (assert no dangling dependencies on shutdown).
4. Documentation automation (generate matrices from code annotations).

---

## Backlog / Open Items

| Item | Category | Notes | Priority |
|------|----------|-------|----------|
| Remove reverse maps | Safety | Delete unused reverse_* maps & insertions | High |
| Cycle detection on add | Safety | Reject edges forming cycles | High |
| Release recursion guard | Safety | Visited set / assert | High |
| Unloader contract docs | Lifecycle | Specify ordering & constraints | High |
| Thread safety policy assert | Concurrency | Enforce single-thread usage (Phase 1) | High |
| Verbose eviction diagnostics | Observability | Runtime log-level controlled | Medium |
| Debug dependent scan helper | Tooling | On-demand dependent enumeration (debug) | Medium |
| Graph snapshot API | Tooling | Structured dependency export (post Phase 1) | Low |
| Formal unloader function registration | Lifecycle | Per-type cleanup & GPU release | High |
| UploadQueue prototype | Async | MVP with event completion | High |
| Coroutine wrapper layer | Async | Transitional API around sync loaders | High |
| Hot reload invalidation graph | Tooling | Watch + recompute dependents | Medium |
| Streaming prioritization heuristics | Streaming | Distance, importance, LOD gating | Medium |
| Memory budget tracking | Memory | Soft & hard budget thresholds | Medium |
| Dependency analyzer CLI | Tooling | Graphviz / JSON output | Medium |
| Performance benchmark suite | Testing | Measure load stage timings | Medium |
| Automated doc status extraction | Docs | Avoid manual table drift | Low |

---

## Glossary

| Term | Definition |
|------|------------|
| Asset | First-class loadable descriptor (Geometry, Material, etc.) |
| Resource | Raw data blob (TextureResource, BufferResource) referenced by index |
| Embedded Descriptor | Metadata struct existing only inside an asset descriptor (e.g., MeshDesc) |
| LoaderContext | Struct passed to loaders containing loader state & dependency hooks |
| Upload Queue | Planned asynchronous staging→GPU submission mechanism |
| Residency | Management of GPU memory presence & eviction |

---

## Cross-Reference Map

| Topic | File |
|-------|------|
| PAK Layout & Alignment | `chunking.md` |
| Async Design Details | `asset_loader.md` |
| Dependency & Cache Mechanics | `deps_and_cache.md` |
| Entity Relationships | `overview.md` |

---

## Update Policy

All future status or roadmap edits must be applied here first; other docs should only link to the relevant anchors. CI (future) can lint for stray status tables elsewhere.

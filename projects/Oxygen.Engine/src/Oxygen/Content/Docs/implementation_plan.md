<!-- Canonical implementation status & planning hub for Oxygen Content subsystem -->

# Content Subsystem Implementation Plan

**Last updated:** 13 Aug 2025

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

Synchronous loading, caching, and forward-only dependency registration are
implemented (see `AssetLoader.*`, `ResourceTable.h`, `PakFile.*`). Safe
unloading protections (cycle rejection + recursion guard + single-thread policy)
are in place; remaining work is completion of specific ordering diagnostics &
tests (see missing tests list below). Async pipeline, streaming, residency
management, and hot-reload are still pending design → implementation transition.

No SceneAsset implementation exists yet; integration with the Scene subsystem
will depend on establishing a `SceneAsset` descriptor + build tooling. Internal
key separation (`InternalResourceKey.h`) is present for encapsulation of
resource indices; future unification/clarification doc is needed.

---

## Detailed Feature Matrix

Legend: ✅ Complete | 🔄 Partial/in-progress | ❌ Missing | 🧪 Prototype (code
present but not production-ready)

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
| Safe Asset Unloading | 🔄 Partial | Core logic present; ordering tests & docs pending |
| Asset Caching | ✅ Complete | Asset cache with ref counts |
| Hot Reload | ❌ Missing | No invalidation or watcher |

### Asynchronous System (Design Stage → Early Prototype)

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

### Phase 1 (Foundation) – COMPLETE

Scope: Core caching, dependency registration, reference counting, baseline
synchronous loaders, safe unloading protections.

Outcome Summary:

* Forward-only dependency tracking with cycle detection and recursion guard
  implemented.
* Reverse dependency maps removed (simpler, memory-stable design validated under
  soak).
* Single-thread confinement policy enforced (foundation phase) ahead of async
  introduction.
* Verbose eviction diagnostics available (log level gated).
* All core infrastructure & asset loading matrix items (except Hot Reload) at ✅.

### Phase 2 (Async CPU Pipeline & Renderer Boundary)

Objective: Add asynchronous, non-blocking CPU-side asset acquisition ending at a
"DecodedCPUReady" state (asset + dependencies decoded & cached) without
performing any GPU uploads or assuming GPU residency. Establish a narrow,
explicit boundary for later renderer-driven materialization.

Scope (Content module only):

1. Coroutine task wrappers around current synchronous loaders (file IO +
   decode).
2. ThreadPool integration (`co_threadpool`) for decode / transcode / packing
   work.
3. Non-blocking async file read abstraction (initially still synchronous under
  the hood; interface designed for future true async I/O swap-in).
4. Dual API: `Load(const AssetKey&)` (legacy sync) layered atop `LoadAsync(const
  AssetKey&, CancellationToken)`.
5. Minimal cancellation primitive propagated through loader contexts; cancelled
  operations guarantee no partial cache insertion.
6. Consolidated deduplication: concurrent `LoadAsync` for same key coalesce to a
  single in-flight task (winner populates cache; others await result).
7. Explicit state enum introduced (e.g., `AssetLoadState::{Loading,
  DecodedCPUReady}`) clarifying that GPU residency is external.
8. Definition of a lightweight bridge descriptor (e.g.,
  `GpuMaterializationInfo`) stored with decoded assets but not acted upon by
  Content.

Out of Scope (moved to Renderer or future phases): GPU upload queue, staging
buffer management, residency / LRU, GPU memory budgets, eviction policies.

Deliverables & Acceptance:

* API: `task<AssetHandle> LoadAsync(const AssetKey&, CancellationToken)` returns
  once asset + dependencies reach DecodedCPUReady (no GPU blocking).
* Cancellation test: cancel mid-file read → no cache entry, no leaked temp
  buffers.
* Deduplication test: N parallel `LoadAsync` for same key results in exactly one
  decode execution (instrumented counter == 1).
* Dependency await test: dependent asset `LoadAsync` suspends until all direct
  dependencies DecodedCPUReady (not GPU resident).
* Performance sanity: Bulk async load of geometry assets saturates ThreadPool
  with main thread idle except at `co_await` suspend points.
* Documentation: Updated glossary & comments to remove any implication that
  Content ensures GPU residency.
* Bridge descriptor unit test: verifies `GpuMaterializationInfo` populated with
  expected format/usage metadata without invoking renderer code.

### Phase 3 (Hot Reload, Streaming Metadata & Analytics)

Objective: Add dynamic content invalidation, introduce CPU-side streaming /
chunk metadata & prioritization signals (still independent of GPU residency),
and provide analytical tooling for dependency & usage insights.

Scope:

1. Hot Reload: file hash watcher → invalidation queue → safe unload & reload of
   affected assets; dependency cascade (rebuild dependents to DecodedCPUReady).
2. Chunk / Streaming Metadata: define per-asset chunk descriptors & partial load
   table (e.g., mesh LOD segments, texture mip groups) stored in Content cache.
3. Priority & Prefetch Hints: lightweight API (`SetAssetPriority`,
   `PrefetchAsync`) that schedules background decode ahead of expected use (no
   GPU action).
4. Progressive/Partial Decode Hooks: optional staged decode phases (e.g., header
   parse first → schedule heavy body decode) enabling fast placeholder data.
5. Dependency Analyzer Tool: export graph (JSON + Graphviz) with reference
   counts, shared dependency fan-out statistics.
6. Observability: instrumentation counters (decode time, IO time, queue wait)
   exposed via a Content diagnostics API.

Out of Scope: Actual GPU memory budgets, GPU eviction, LRU of GPU resources
(renderer phase).

Deliverables & Acceptance:

* Hot reload test: modify underlying file → asset & dependents reach new hash
  version; old handles safely replaced.
* Prefetch test: calling `PrefetchAsync` on N assets triggers background decode
  without blocking main thread.
* Partial decode test: staged loader produces minimal placeholder then completes
  full decode later (verified via timing / state).
* Analyzer CLI produces JSON with nodes = assets, edges = dependencies;
  validated by a unit/integration test snapshot.
* Instrumentation: metrics accessible & unit tested for monotonicity / reset
  behavior.

### Phase 4 (Quality, Benchmarking & Automation)

Objective: Harden the Content subsystem with comprehensive performance / memory
metrics, diagnostic validation, and automated documentation to reduce drift.

Scope:

1. Performance benchmarks: cold load vs warm cache vs parallel burst; measure IO
   latency, decode time, scheduling overhead.
2. Memory diagnostics: peak decoded bytes, per-asset-type footprint,
   fragmentation or slack (if applicable).
3. Dependency integrity checks: shutdown assertions for no dangling dependencies
   / leaked in-flight tasks.
4. Telemetry & Histograms: expose structured event stream (load start/end,
   decode phases, cancellation) plus percentile histograms.
5. Automated documentation generation: scripts extract feature matrix & status
   from annotated code / registry macros.
6. Reliability tests: fuzz or randomized cancellation / interleaving to detect
   race conditions.

Acceptance:

* Benchmark suite produces stable metrics; CI gate can compare against
  thresholds.
* Telemetry API consumed in a sample tool printing histogram percentiles.
* Integrity test simulates random load/cancel sequences → zero leaks &
  consistent ref counts.
* Generated docs match manually maintained sections (diff-free) or report
  discrepancies.

---

## Backlog / Open Items

| Item | Category | Notes | Priority |
|------|----------|-------|----------|
| Unloader contract docs | Lifecycle | Specify ordering & constraints | High |
| Verbose eviction diagnostics | Observability | Runtime log-level controlled | Medium |
| Graph snapshot API | Tooling | Structured dependency export (post Phase 1) | Low |
| Formal unloader function registration | Lifecycle | Per-type cleanup & GPU release | High |
| Coroutine wrapper layer | Async | Transitional API around sync loaders | High |
| Async file I/O abstraction | Async | Interface + stub impl (future OS async) | High |
| In-flight deduplication map | Async | Single execution per key guarantee | High |
| Hot reload invalidation graph | Tooling | Watch + recompute dependents | Medium |
| Streaming prioritization heuristics | Streaming | Priority & prefetch hints (no GPU) | Medium |
| Chunk metadata schema | Streaming | Per-asset chunk & partial decode descriptors | High |
| Dependency analyzer CLI | Tooling | Graphviz / JSON output | Medium |
| Performance benchmark suite | Testing | Measure load stage timings | Medium |
| Automated doc status extraction | Docs | Avoid manual table drift | Low |
| SceneAsset descriptor & importer | Asset | Define schema + loader + toolchain | High |
| Async test harness (fake I/O) | Testing | Deterministic coroutine/unit tests | High |
| Cancellation token propagation | Async | Uniform cancellation semantics | High |
| Bridge descriptor (GpuMaterializationInfo) | Interface | Data-only; consumed by Renderer | High |
| Memory mapping prototype | Performance | Evaluate zero-copy feasibility | Low |
| PAK diff tool | Tooling | Compare two PAKs (size, hash, delta) | Medium |
| Partial load recovery (crash safety) | Robustness | Resume or rollback incomplete loads | Low |
| Content versioning & migration hooks | Lifecycle | Descriptor schema evolution | Medium |
| Unified key doc (Asset/Resource/Internal) | Docs | Clarify key domains & constraints | Low |
| Log capture helper for tests | Testing | Assert diagnostics without global state | Medium |
| Dependency graph visualization (interactive) | Tooling | Build atop analyzer JSON | Low |
| Instrumentation counters API | Observability | Decode/IO timing, queue wait metrics | High |
| Progressive decode staging support | Streaming | Header-first fast path | Medium |

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

All future status or roadmap edits must be applied here first; other docs should
only link to the relevant anchors. CI (future) can lint for stray status tables
elsewhere.

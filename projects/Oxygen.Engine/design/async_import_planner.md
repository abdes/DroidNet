# Async Import Planner + Readiness Tracking (LLD)

**Status:** Design Clarification (PakFormat‑aligned)
**Date:** 2026-01-16
**Authoritative source:** [src/Oxygen/Data/PakFormat.h](src/Oxygen/Data/PakFormat.h)

---

## 1. Purpose and Scope

This document defines the **low‑level design** for the async import planner and
readiness tracking, grounded in the PAK format definitions. It covers:

- Planner data model and graph lifecycle.
- Linear plan construction (stable topological order).
- Readiness tracking contract and coroutine execution flow.

It **does not** redefine pipeline architecture or I/O responsibilities; those
live in [design/async_import_pipeline_v2.md](async_import_pipeline_v2.md).

### File Layout (Planned)

- [src/Oxygen/Content/Import/Async/ImportPlanner.h](src/Oxygen/Content/Import/Async/ImportPlanner.h)
- [src/Oxygen/Content/Import/Async/ImportPlanner.cpp](src/Oxygen/Content/Import/Async/ImportPlanner.cpp)

### Relationship to the Async Import Architecture

This planner is a **job‑scoped** component used by `ImportJob::ExecuteAsync()`
in the import thread (see
[design/async_import_pipeline_v2.md](async_import_pipeline_v2.md)). It owns the
dependency graph and readiness tracking only. Pipelines remain compute‑only and
emitters perform all I/O. The planner does not parse importer formats and does
not own any cooked output; the job stores pipeline results in a job‑level cache
after emission.

### Non‑Goals

- Introducing new asset/resource types beyond those defined in
  [src/Oxygen/Data/PakFormat.h](src/Oxygen/Data/PakFormat.h).
- Defining pipeline internals (see the per‑pipeline design docs).

---

## 2. Terminology (PakFormat‑Aligned)

- **Resource**: A binary blob referenced by a **ResourceIndexT** in a table.
  Types currently defined: **Texture**, **Buffer**, **Audio**.
- **Asset**: A descriptor blob referenced by an **AssetKey** (128‑bit GUID).
  Types currently defined: **Material**, **Geometry**, **Scene**.

> **Out of scope:** Animation and morph assets are **not** defined in
> PakFormat v2–v4 and therefore are **not** part of this planner LLD.

---

## 3. Dependency Map (Authoritative)

Dependencies must match the **exact** fields defined in
[src/Oxygen/Data/PakFormat.h](src/Oxygen/Data/PakFormat.h). The planner must
not infer additional dependency types.

| Consumer | Depends on | Source field(s) | Notes |
| --- | --- | --- | --- |
| Material asset | Texture resources | `MaterialAssetDesc::*_texture` | Only for authored textures. Texture index `0` is the fallback texture. If `kMaterialFlag_NoTextureSampling` is set, no texture dependencies exist. |
| Geometry asset | Buffer resources | `MeshDesc::info.standard.vertex_buffer`, `index_buffer` | Applies only to **standard** meshes. Procedural meshes have no buffer dependencies. |
| Geometry asset | Material assets | `SubMeshDesc::material_asset_key` | One dependency per referenced material asset. |
| Scene asset | Geometry assets | `RenderableRecord::geometry_key` | v2–v4 scenes. |
| Scene asset (v3+) | Environment assets | `SkyLightEnvironmentRecord::cubemap_asset`, `SkySphereEnvironmentRecord::cubemap_asset` | Only when those records are present. |

No other cross‑record dependencies are defined in PakFormat v2–v4.

Dependency management is step-centric: a dependency only requires that the
producer step has completed. The planner does not need to reason about whether
a step is a resource or asset pipeline; the pipeline type is derived from the
plan item kind when retrieving results. Produced results are stored by the job
in a result cache keyed by `PlanItemId`.

---

## 4. Planner Data Model

### 4.1 Core Types

```cpp
enum class PlanItemKind : uint8_t {
  kTextureResource,
  kBufferResource,
  kAudioResource,
  kMaterialAsset,
  kGeometryAsset,
  kSceneAsset,
};
inline constexpr size_t kPlanKindCount = 6;
using PlanItemId = oxygen::NamedType<uint32_t, struct PlanItemIdTag,
  // clang-format off
  oxygen::DefaultInitialized,
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable
  // clang-format on
>;

// Strongly typed handle for importer-owned payload references.
using WorkPayloadHandle = oxygen::NamedType<void*, struct WorkPayloadHandleTag,
  // clang-format off
  oxygen::DefaultInitialized,
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable
  // clang-format on
>;

struct DependencyToken {
  PlanItemId producer = 0;
};

struct ReadinessEvent {
  co::Event event;
  bool ready = false;
};

struct ReadinessTracker {
  // One entry per producer dependency (deduplicated by producer step).
  std::span<const PlanItemId> required;
  std::span<bool> satisfied;              // same size as required
  ReadinessEvent ready_event;

  auto IsReady() const noexcept -> bool;
  auto MarkReady(const DependencyToken& token) -> bool; // true on transition
};

struct PlanItem {
  PlanItemId id = 0;
  PlanItemKind kind = PlanItemKind::kTextureResource;
  std::string debug_name;
  WorkPayloadHandle work_handle {}; // opaque key into importer state
};

struct PlanStep {
  PlanItemId item_id = 0;
  std::span<const ReadinessEvent* const> prerequisites;
};

// Planner owns the backing storage for prerequisites. Each PlanStep references
// a slice of a planner-owned vector of ReadinessEvent*.

// Readiness trackers are stored separately (e.g., in planner-owned arrays
// keyed by PlanItemId) to keep PlanItem minimal.

// Plan items are stored in a planner-owned table keyed by PlanItemId. The
// id is not used as a constructor; it is a lookup key into that table.
```

### 4.2 Plan Items and Steps

- **Plan item**: A declared asset/resource in the import job.
- **Plan step**: The execution unit generated by `MakePlan()`.

Current design is **1:1** (each item produces one step). The item/step
distinction is kept to allow future coalescing or batching without changing the
input model.

Pipeline selection is an internal detail of the planner. Callers ask the
planner for a pipeline type given a `PlanItemId`, and the planner resolves it
based on the item's kind.

### 4.3 Planner Storage and ID Mapping

`PlanItemId` is a strong type and acts as a lookup key into planner‑owned
tables. The planner assigns IDs in registration order and stores items in a
contiguous table to enable stable iteration and deterministic ordering.

```cpp
// Sketch of planner-owned storage (implementation detail)
std::vector<PlanItem> items_;          // indexed by PlanItemId
std::vector<ReadinessEvent> events_;   // indexed by PlanItemId
std::vector<ReadinessTracker> trackers_; // indexed by PlanItemId
std::vector<const ReadinessEvent*> prerequisites_storage_;

std::array<std::optional<oxygen::TypeId>, kPlanKindCount> pipeline_registry_;

auto ImportPlanner::Item(PlanItemId item) -> PlanItem&
{
  const auto u_item = item.get();
  return items_.at(u_item);
}
```

The ID is never used as a constructor; it is only a lookup key into these
tables.

### 4.4 Job-Level Result Cache (Not Planner-Owned)

The job maintains a result cache keyed by `PlanItemId`. It stores the output of
pipeline execution after collection and emission, and is the source of
downstream inputs. The cache stores full per‑pipeline `WorkResult` values, not
just indices or keys. Examples:

- Texture pipeline → `TexturePipeline::WorkResult`
- Buffer pipeline → `BufferPipeline::WorkResult`
- Material pipeline → `MaterialPipeline::WorkResult`
- Geometry pipeline → `GeometryPipeline::WorkResult`
- Scene pipeline → `ScenePipeline::WorkResult`

These results match the per‑pipeline contracts in:
[design/texture_work_pipeline_v2.md](design/texture_work_pipeline_v2.md),
[design/buffer_work_pipeline_v2.md](design/buffer_work_pipeline_v2.md),
[design/material_work_pipeline_v2.md](design/material_work_pipeline_v2.md),
[design/geometry_work_pipeline_v2.md](design/geometry_work_pipeline_v2.md), and
[design/scene_work_pipeline_v2.md](design/scene_work_pipeline_v2.md).

The planner never stores these values; it only signals when a producer step is
complete.

---

## 5. Planner API (Job‑Scoped)

```cpp
class ImportPlanner final {
public:
  //=== High-level plan construction ===//

  auto AddTextureResource(std::string debug_name,
    WorkPayloadHandle work_handle) -> PlanItemId;
  auto AddBufferResource(std::string debug_name,
    WorkPayloadHandle work_handle) -> PlanItemId;
  auto AddAudioResource(std::string debug_name,
    WorkPayloadHandle work_handle) -> PlanItemId;
  auto AddMaterialAsset(std::string debug_name,
    WorkPayloadHandle work_handle) -> PlanItemId;
  auto AddGeometryAsset(std::string debug_name,
    WorkPayloadHandle work_handle) -> PlanItemId;
  auto AddSceneAsset(std::string debug_name,
    WorkPayloadHandle work_handle) -> PlanItemId;
  auto AddDependency(PlanItemId consumer, PlanItemId producer) -> void;

  //=== Pipeline registration (testable) ===//

  template <ImportPipeline Pipeline>
  auto RegisterPipeline(PlanItemKind kind) -> void
  {
    const auto index = static_cast<size_t>(kind);
    pipeline_registry_.at(index) = Pipeline::ClassTypeId();
  }

  auto MakePlan() -> std::vector<PlanStep>; // freeze graph + optimize

  auto Item(PlanItemId item) -> PlanItem&;

  [[nodiscard]] auto PipelineTypeFor(PlanItemId item) const noexcept
    -> std::optional<oxygen::TypeId>;

  auto Tracker(PlanItemId item) -> ReadinessTracker&;
  auto ReadyEvent(PlanItemId item) -> ReadinessEvent&;
};

### Pipeline Resolution

`PipelineTypeFor(item)` resolves the pipeline type from the item's kind using a
planner‑owned registry. The registry is populated via `RegisterPipeline(...)`
so tests can inject mock pipeline types without instantiating real pipelines.

Pipeline type IDs come from the typed pipeline classes (see
[src/Oxygen/Composition/Typed.h](src/Oxygen/Composition/Typed.h) and
[src/Oxygen/Composition/Object.h](src/Oxygen/Composition/Object.h)).

```cpp
auto ImportPlanner::PipelineTypeFor(PlanItemId item) const noexcept
  -> std::optional<oxygen::TypeId>
{
  const auto& plan_item = Item(item);
  const auto index = static_cast<size_t>(plan_item.kind);
  return pipeline_registry_.at(index);
}
```

### API Invariants

- Each `Add*` method must be called only for its corresponding item kind.
- Each `PlanItemKind` used in a plan must have a registered pipeline type.
- `RegisterPipeline<Pipeline>()` requires `Pipeline` to satisfy
  `ImportPipeline` (see
  [src/Oxygen/Content/Import/Async/ImportPipeline.h](src/Oxygen/Content/Import/Async/ImportPipeline.h)).
- `AddDependency(consumer, producer)` must represent a valid PakFormat
  dependency (see Section 3).
- Dependencies are deduplicated **by producer step** per consumer.

---

## 6. Plan Construction (Stable Topological Order)

`MakePlan()` performs:

1) Build in‑degree for each **item** from dependency edges.
2) Insert all zero‑in‑degree items into a stable priority queue
  (registration order from `Add*` calls is the tie‑breaker).
3) Pop items to build the **step sequence**.
4) Decrement in‑degree of dependents; when 0, enqueue.
5) If items remain, report a **cycle** (blocking diagnostic).

The plan is **linear** (a sequence of steps), while readiness gates each step
using events.

### Sealing Rules

- `MakePlan()` sets `sealed_ = true` and initializes readiness trackers.
- After sealing, `Add*` and `AddDependency` must reject calls
  (debug assertion + error return).
- `MakePlan()` validates that all item kinds in the plan have registered
  pipeline types; missing registrations are blocking diagnostics.
- `MakePlan()` builds `PlanStep.prerequisites` as spans into
  `prerequisites_storage_`.
- The plan is immutable after construction. Only readiness state mutates.

---

## 7. Readiness Tracking

### 7.1 Required Dependencies

For each consumer step, `ReadinessTracker.required` contains a list of
producer step IDs (one per **unique producer** item). The tracker only tracks
completion of those producer steps.

Dependencies are strictly between plan steps. They do not depend on whether a
step represents a resource or asset pipeline; only completion matters.

`PlanStep.prerequisites` is built by converting each producer dependency into
its corresponding `ReadinessEvent*` from the planner. Each dependency points
to `planner.ReadyEvent(producer_item_id)`, so awaiting prerequisites is just
waiting on those events to become ready.

### 7.2 MarkReady Contract

`ReadinessTracker::MarkReady(token)`:

- Locate the slot matching `token.producer`.
- Set `satisfied[i] = true`.
- If all entries are satisfied and `ready_event.ready == false`, set it true
  and signal `ready_event.event`.

Duplicate `MarkReady` calls for the same producer are ignored.

### 7.3 Consuming Produced Results

Pipelines that require results from producer steps must retrieve them **after**
readiness is signaled. Results are obtained from a job-level result cache keyed
by the producer `PlanItemId`. The cache is populated when `SubmitWork()`
collects a pipeline result and emits it.

---

## 8. Execution Contract (Importer Loop)

```cpp
for (const auto& step : plan) {
  std::vector<co::Co<>> waits;
  waits.reserve(step.prerequisites.size());
  for (auto* ev : step.prerequisites) {
    waits.push_back([ev]() -> co::Co<> {
      if (!ev->ready) {
        co_await ev->event.Wait();
      }
    }());
  }

  co_await co::AllOf(std::move(waits));

  const auto pipeline_type_id = planner.PipelineTypeFor(step.item_id);
  OXGN_ASSERT(pipeline_type_id.has_value());
  co_await SubmitWork(*pipeline_type_id, step.item_id);
}

The readiness wait uses `co::AllOf` from
[src/Oxygen/OxCo/Algorithms.h](src/Oxygen/OxCo/Algorithms.h) over the
prerequisite events. When `step.prerequisites` is empty, `AllOf` completes
immediately. The planner sets `ready=true` and signals the event when all
producer steps for that dependency complete.
```

Each `SubmitWork`:

1) Submits to the pipeline (bounded backpressure).
2) Collects a `WorkResult` and records it in the job-level result cache
  (keyed by `PlanItemId`).
3) Emits payloads via emitters (assigning stable indices or asset keys).
4) Calls `MarkReady(...)` on dependent trackers.

---

## 9. Emission → Readiness Flow (Examples)

### Texture → Material

1) `TexturePipeline` returns `CookedTexturePayload`.
2) `TextureEmitter.Emit(payload)` returns a texture table index.
3) Job caches the `TexturePipeline::WorkResult` keyed by `texture_item` and
  updates readiness:

```cpp
planner.Tracker(material_item)
  .MarkReady({texture_item});
```

### Material → Geometry

1) `MaterialPipeline` returns a cooked material descriptor.
2) `AssetEmitter.Emit(...)` returns the material asset key.
3) Job caches the `MaterialPipeline::WorkResult` keyed by `material_item` and
  updates readiness:

```cpp
planner.Tracker(geometry_item)
  .MarkReady({material_item});
```

### Geometry → Scene

1) `GeometryPipeline` emits `.ogeo` and returns the geometry asset key.
2) Job caches the `GeometryPipeline::WorkResult` keyed by `geometry_item` and
  updates readiness.

---

## 10. `content_hash` Gating Rule (PakFormat‑Aligned)

- Resource hashes (`TextureResourceDesc::content_hash`,
  `BufferResourceDesc::content_hash`) are computed **after** the resource bytes
  are finalized, on the ThreadPool.
- Asset hashes (`AssetHeader::content_hash`) are computed **after** all
  readiness dependencies are satisfied and the descriptor bytes are final.
- Hashing always runs on the ThreadPool.

---

## 11. Cancellation

- Job nursery cancellation stops all pipeline workers.
- Awaiting readiness events must be cancellation‑safe.
- On cancellation, the importer aborts traversal and finalizes the session
  with `success=false` and diagnostic code `import.cancelled`.

---

## 12. Diagnostics

- Graph cycles or invalid dependencies are **blocking** errors.
- Missing pipeline registrations for used kinds are **blocking** errors.
- Readiness events never fire on invalid plans.
- All pipeline failures propagate as `ImportDiagnostic` records.

---

## 13. LLD Checklist (PakFormat‑Aligned)

- [ ] Deterministic, stable topological order
- [ ] Per‑step readiness tracker with awaitable event
- [ ] Produced results stored in a job-level cache (keyed by PlanItemId)
- [ ] Pipeline registry enables planner-only tests (no real pipelines)
- [ ] Emission updates readiness tokens (per dependency map)
- [ ] ThreadPool‑only hashing after readiness
- [ ] Cancellation‑safe readiness awaits
- [ ] Cycle detection and diagnostics
- [ ] No references to non‑PakFormat asset types (animation/morph)

---

## 14. TODOs (Implementation + Tests)

### 14.1 Implementation TODOs

- [x] Create ImportPlanner public header and source files:
  - [x] src/Oxygen/Content/Import/Async/ImportPlanner.h
  - [x] src/Oxygen/Content/Import/Async/ImportPlanner.cpp
- [x] Define core types per Section 4.1:
  - [x] PlanItemKind enum and kPlanKindCount
  - [x] PlanItemId strong type
  - [x] WorkPayloadHandle strong type
  - [x] DependencyToken, ReadinessEvent, ReadinessTracker, PlanItem, PlanStep
- [x] Implement ReadinessTracker logic:
  - [x] IsReady() and MarkReady() per Section 7.2
  - [x] Deduplicate producers in required list
- [x] Implement ImportPlanner storage layout per Section 4.3:
  - [x] items_ table (PlanItem)
  - [x] events_ table (ReadinessEvent)
  - [x] trackers_ table (ReadinessTracker)
  - [x] prerequisites_storage_ (ReadinessEvent* slices)
  - [x] pipeline_registry_ (TypeId per PlanItemKind)
- [x] Implement Add* methods for each PlanItemKind:
  - [x] Enforce kind correctness; store debug name + work handle
  - [x] Allocate PlanItemId in registration order
- [x] Implement AddDependency(consumer, producer):
  - [x] Validate sealed_ == false
  - [x] Record dependency edge (consumer -> producer)
  - [x] Deduplicate by producer per consumer
- [x] Implement RegisterPipeline<Pipeline>(kind) registration
- [x] Implement PipelineTypeFor(item) resolver
- [x] Implement Item(PlanItemId), Tracker(PlanItemId), ReadyEvent(PlanItemId)
- [x] Implement MakePlan():
  - [x] Validate all used kinds are registered
  - [x] Build in‑degree by item from dependency edges
  - [x] Stable topological ordering by registration order
  - [x] Detect and report cycles (blocking diagnostic)
  - [x] Build PlanStep list with prerequisite spans
  - [x] Initialize readiness trackers and events
  - [x] Set sealed_ flag and prevent further mutation
- [x] Wire in diagnostics types and error handling conventions
- [x] Add minimal logging/trace hooks (if required by import subsystem)

### 14.2 Test TODOs (Fake/Mock Pipelines)

- [x] Create test file(s) under appropriate test target (e.g.
  src/Oxygen/Content/Import/Async/ImportPlanner_basic_test.cpp or equivalent)
- [x] Define fake pipeline types satisfying ImportPipeline:
  - [x] Minimal mock pipeline classes with static ClassTypeId()
  - [x] No dependency on real pipeline implementations
  - [x] Register via RegisterPipeline<MockPipeline>()
- [x] Add test fixtures following unit test rules (AAA, doc comments, NOLINT_*):
  - [x] Basic planner fixture
  - [x] Error/cycle fixture
  - [x] Edge‑case fixture (empty plan, single item)
- [x] Plan construction tests:
  - [x] Stable topological order with mixed kinds
  - [x] Tie‑breaker uses registration order
  - [x] Deduplication of producer dependencies per consumer
  - [x] Missing pipeline registration is blocking error
  - [x] Add* and AddDependency reject after sealed
  - [x] Cycle detection produces blocking diagnostic
- [x] Readiness tracker tests:
  - [x] MarkReady transitions to ready when all producers satisfied
  - [x] Duplicate MarkReady does not re‑signal
  - [x] Empty required list is ready
  - [x] ReadyEvent is signaled exactly once
- [x] PlanStep prerequisites tests:
  - [x] Prerequisite list references producer ReadyEvent instances
  - [x] Prerequisites span uses planner‑owned storage (stable slices)
  - [x] Steps with no prerequisites have empty spans
- [x] Pipeline resolution tests:
  - [x] PipelineTypeFor returns registered type for each kind
  - [x] Unregistered kind returns empty optional
- [x] Diagnostics tests:
  - [x] Invalid dependency (if validated) reports proper diagnostic
  - [x] Cycle diagnostics identify involved items (if supported)


---

## See Also

- [design/async_import_pipeline_v2.md](async_import_pipeline_v2.md)
- [design/material_work_pipeline_v2.md](material_work_pipeline_v2.md)
- [design/geometry_work_pipeline_v2.md](geometry_work_pipeline_v2.md)
- [design/scene_work_pipeline_v2.md](scene_work_pipeline_v2.md)
- [src/Oxygen/Data/PakFormat.h](src/Oxygen/Data/PakFormat.h)# Async Import Planner + Readiness Tracking (LLD)

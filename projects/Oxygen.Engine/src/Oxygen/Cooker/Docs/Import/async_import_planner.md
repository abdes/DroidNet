# Import Planner and Readiness Tracking

`ImportPlanner` builds the dependency plan of one model import and tracks the
readiness of each plan item. It is job-scoped and not thread-safe: the model
job creates one, registers items and dependencies, calls `MakePlan()`, and the
`WorkDispatcher` runs the plan (see
[async_import_pipeline_v2.md](async_import_pipeline_v2.md#model-import-plan)).

The planner owns the graph and the readiness state only. It does not parse
formats, own cooked output or store results; payloads are opaque handles into
the job's `WorkPayloadStore`, and pipelines stay compute-only.

Source:
[ImportPlanner.h](../../Import/Internal/ImportPlanner.h) and
[ImportPlanner.cpp](../../Import/Internal/ImportPlanner.cpp).

## Plan items

A plan item is one unit of work: a resource or an asset the job will produce.

| `PlanItemKind`     | Label       | Produces              | Pipeline            |
| ------------------ | ----------- | --------------------- | ------------------- |
| `kTextureResource` | `Texture`   | A texture table row   | `TexturePipeline`   |
| `kBufferResource`  | `Buffer`    | A buffer table row    | `BufferPipeline`    |
| `kMaterialAsset`   | `Material`  | A material descriptor | `MaterialPipeline`  |
| `kMeshBuild`       | `MeshBuild` | A cooked mesh payload | `MeshBuildPipeline` |
| `kGeometryAsset`   | `Geometry`  | A geometry descriptor | `GeometryPipeline`  |
| `kSceneAsset`      | `Scene`     | A scene descriptor    | `ScenePipeline`     |

`kPlanKindCount` is 6. The kinds are listed in
[ImportPipeline.h](../../Import/Internal/ImportPipeline.h). There is no audio
kind.

```cpp
using PlanItemId = NamedType<uint32_t, struct PlanItemIdTag, ...>;
using WorkPayloadHandle = NamedType<void*, struct WorkPayloadHandleTag, ...>;

struct PlanItem { PlanItemId id; PlanItemKind kind;
                  std::string debug_name; WorkPayloadHandle work_handle; };
struct PlanStep { PlanItemId item_id;
                  std::vector<PlanItemId> prerequisites; };
```

`PlanItemId` is the item's registration index and a lookup key into the
planner's tables, never a constructor argument. A `PlanStep` is one item and
the producers it waits for; the plan has one step per item.

## Dependencies

The planner infers nothing. The job declares each edge with
`AddDependency(consumer, producer)`:

| Consumer | Depends on                                          |
| -------- | --------------------------------------------------- |
| Material | The textures it binds                               |
| Geometry | Its mesh build, and the materials its submeshes use |
| Scene    | Every geometry                                      |

A consumer needs only that the producer step completed. Duplicate edges
(same consumer and producer) are ignored. The job keeps the produced results in
its own cache keyed by `PlanItemId`; the planner only signals completion.

## API

```cpp
class ImportPlanner final {
public:
  auto AddTextureResource(std::string debug_name, WorkPayloadHandle) -> PlanItemId;
  auto AddBufferResource(std::string debug_name, WorkPayloadHandle) -> PlanItemId;
  auto AddMaterialAsset(std::string debug_name, WorkPayloadHandle) -> PlanItemId;
  auto AddGeometryAsset(std::string debug_name, WorkPayloadHandle) -> PlanItemId;
  auto AddMeshBuild(std::string debug_name, WorkPayloadHandle) -> PlanItemId;
  auto AddSceneAsset(std::string debug_name, WorkPayloadHandle) -> PlanItemId;
  auto AddDependency(PlanItemId consumer, PlanItemId producer) -> void;

  template <ImportPipeline Pipeline>
  auto RegisterPipeline(PlanItemKind kind) -> void;

  auto MakePlan() -> std::vector<PlanStep>;

  auto Item(PlanItemId item) -> PlanItem&;
  auto PipelineTypeFor(PlanItemId item) const noexcept
    -> std::optional<TypeId>;
  auto Tracker(PlanItemId item) -> ReadinessTracker&;
  auto ReadyEvent(PlanItemId item) -> ReadinessEvent&;
};
```

`RegisterPipeline` maps a kind to the typed pipeline class (`Pipeline` must
satisfy `ImportPipeline`), so tests can register mock pipelines.
`PipelineTypeFor` resolves an item's pipeline type from its kind.

## MakePlan

`MakePlan()` freezes the graph and returns the steps in a stable topological
order:

1. Every item's kind must have a registered pipeline.
2. Take the items with no prerequisites, sorted by id, as the first batch.
3. For each item in a batch, append its step and decrement the pending count of
   its dependents; a dependent that reaches zero joins the next batch, which is
   sorted by id.
4. The plan is complete when every item has a step.

Within a batch, registration order is the tie-breaker. After `MakePlan()` the
planner is sealed: `Add*`, `AddDependency` and a second `MakePlan()` fail. A
missing registration, a cycle and an out-of-range id are `CHECK` failures
(program errors), not diagnostics.

## Readiness

`MakePlan()` creates one `ReadinessEvent` (`co::Event` plus a `ready` flag) and
one `ReadinessTracker` per item. A tracker lists the item's unique producers
(`required`) and a `satisfied` flag for each. An item with no prerequisites is
ready, and its event triggered, when the plan is made.

`ReadinessTracker::MarkReady({producer})` sets the producer's flag. When every
flag is set it sets `ready`, triggers the event and returns true, the
transition. Marking an unknown producer, or marking again after the item is
ready, returns false. `IsReady()` reads the flag.

The `WorkDispatcher` marks a producer complete after it has emitted the
producer's result, and it enqueues each dependent whose tracker becomes ready.

## Execution

The dispatcher keeps ready items in per-kind buckets and submits them
round-robin across the kinds that still have queue capacity. Results are
collected as pipelines finish; each is emitted through the session, which
marks its dependents. A run stops with an error when:

- no pipeline can take the ready work (`import.plan.capacity_blocked`);
- nothing is pending and the plan is incomplete (`import.plan.deadlock`);
- a result cannot be mapped back to its item
  (`import.plan.texture_unmapped`, `buffer_unmapped`, `material_unmapped`,
  `mesh_build_unmapped`, `geometry_unmapped`, `scene_unmapped`);
- a plan input is missing (`import.plan.mesh_missing`,
  `import.plan.material_key_missing`, `import.plan.surface_invalid`).

Cancellation stops the pipelines through the job nursery and the items' stop
tokens, and the job completes as cancelled
(see [Outcome](async_import_pipeline_v2.md#outcome)).

## See also

- [async_import_pipeline_v2.md](async_import_pipeline_v2.md)
- [material_work_pipeline_v2.md](material_work_pipeline_v2.md)
- [geometry_work_pipeline_v2.md](geometry_work_pipeline_v2.md)
- [scene_work_pipeline_v2.md](scene_work_pipeline_v2.md)
- [texture_work_pipeline_v2.md](texture_work_pipeline_v2.md)
- [buffer_work_pipeline_v2.md](buffer_work_pipeline_v2.md)

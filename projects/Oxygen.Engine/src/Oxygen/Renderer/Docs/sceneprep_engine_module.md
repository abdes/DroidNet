# ScenePrep Engine Module Plan (Working Document)

Purpose: Track migration of ScenePrep from ad-hoc invocation inside `Renderer::BuildFrame` to a dedicated `EngineModule` that publishes a fully finalized, deterministic SoA representation consumed by render graph / passes without legacy `RenderItemsList`.

Status Legend:
- [ ] Not started  | [~] In progress | [x] Complete | [!] Pending decision

## 1. Phase Integration Overview

| Engine Phase | ScenePrepModule Responsibility | Notes |
|--------------|--------------------------------|-------|
| TransformPropagation (pre-existing) | (Dependency) world transforms stable | Required before collection |
| Snapshot | (Dependency) immutable snapshot published | Module reads snapshot only |
| ParallelTasks | Run collection pipeline: visibility, LOD, submesh emission, material resolution into staging `ScenePrepState` (no GPU) | Pure read-only snapshot consumption |
| PostParallel | Finalize: dedupe registries, stable order, pass mask, build partitions, produce `PreparedSceneFrame` and atomically publish | Deterministic step |
| FrameGraph | Consumers query published frame; resolve resource residency, material constants, map logical → bindless indices, build GPU `engine::DrawMetadata` array | Late binding of GPU details |
| CommandRecord | (Optional future) build indirect argument buffers if using GPU-driven draws | Separation of concerns |
| AsyncPoll | (Future) background streaming / caching warmup triggered by misses | Deferred |
| FrameEnd | Recycle old frame buffers after GPU safe (epoch/fence check) | Double-buffer reuse |

## 2. Data Structures

### 2.1 Staging (ParallelTasks)
`ScenePrepState` (already exists) gathers raw `RenderItemData` + intermediate lists.

### 2.2 Finalized Publish Object
```cpp
struct PreparedSceneFrame {
	uint64_t frame_seq;                       // Monotonic frame sequence
	std::vector<CollectedItem> items;         // One per visible submesh (immutable snapshot of collection outputs)
	std::vector<MaterialHandle> materials;    // Logical handles aligned with items ordering
	std::vector<GeometryHandle> geometries;   // Logical geometry buffer pair indices (no bindless yet)
	std::vector<PassMask> pass_masks;         // Per-item pass participation bitset
	std::vector<sceneprep::DrawMetadata> cpu_draw_desc; // Lightweight per-draw (first_index, index_count,...)
	PartitionMap partitions;                  // pass_id -> [begin,end)
	ScenePrepMetrics metrics;                 // (future instrumentation)
	bool finalized = false;                   // Guard for consumers
};
```

### 2.3 GPU Translation (FrameGraph)
Transient vectors (not owned by module):
```cpp
std::vector<engine::DrawMetadata> gpu_draw_metadata; // Packed 14 x uint32 per draw (explicit vertex_count added) = 56 bytes
```
Bindless upload via existing `BindlessStructuredBuffer<DrawMetadata>` and `BindlessStructuredBuffer<glm::mat4>`.

## 3. Two DrawMetadata Variants

| Type | Location | Purpose | Action |
|------|----------|---------|--------|
| `sceneprep::DrawMetadata` | `ScenePrep/Types.h` | CPU logical draw description (counts/index ranges/debug IDs) | Consider rename to `CpuDrawDesc` later |
| `engine::DrawMetadata` | `Renderer/Types/DrawMetadata.h` | GPU-facing packed metadata (bindless indices, offsets) | Final upload format |

Translation occurs in FrameGraph phase (late binding). Avoids GPU coupling inside Parallel/PostParallel.

## 4. Deterministic Ordering Rules

Default stable sort (applied in PostParallel finalization) key order:
1. `material_handle`
2. `geometry_handle`
3. `submesh_id`
4. `lod`
5. `draw_id` (stable tiebreaker)

Document + test: identical snapshot → identical serialized `PreparedSceneFrame` (byte equality of pruned metadata arrays excluding padding).

## 5. Publication Mechanism

Double-buffer: two `PreparedSceneFrame` instances.
Steps:
1. ParallelTasks writes to working buffer (not visible).
2. PostParallel finalizes + sets `finalized=true`.
3. Atomic pointer swap (publish pointer + frame_seq).
4. Old buffer retained until reused next frame (no GPU lifetime entanglement because GPU uses separate uploaded buffers each frame).

Accessor:
```cpp
const PreparedSceneFrame* ScenePrepModule::TryGetPreparedFrame(uint64_t expected_seq) const;
```
Returns nullptr if not published or seq mismatch.

## 6. Phase Handlers (Draft Signatures)

```cpp
class ScenePrepModule : public EngineModule {
	// Metadata
	std::string_view GetName() const noexcept override;
	ModulePriority GetPriority() const noexcept override; // choose mid-tier
	ModulePhaseMask GetSupportedPhases() const noexcept override; // ParallelTasks | PostParallel | FrameGraph

	co::Co<> OnParallelTasks(const FrameSnapshot& snapshot) override; // collection
	co::Co<> OnPostParallel(FrameContext& ctx) override;              // finalize + publish
	co::Co<> OnFrameGraph(FrameContext& ctx) override;                // GPU binding translation
};
```

## 7. GPU Binding Translation Steps (FrameGraph)

1. For each geometry handle ensure vertex/index buffers resident (calls renderer mesh resource system).
2. Build mapping logical_geometry -> (vertex_buffer_index, index_buffer_index).
3. Build mapping material_handle -> material constants index (bindless slot).
4. Assign transform_offset = index in world_transforms vector.
5. Populate `engine::DrawMetadata` entries (is_indexed, first_index, base_vertex, etc.).
6. Upload world transforms + draw metadata through bindless structured buffer helpers (mark dirty & EnsureAndUpload).
7. Patch `SceneConstants` bindless slots (already supported by renderer methods) — renderer may query module for needed counts first.

## 8. Incremental Migration Milestones

| ID | Goal | Details | Status |
|----|------|---------|--------|
| M1 | Module skeleton | Add `ScenePrepModule` with empty handlers | [ ] |
| M2 | Parallel collection moved | Invoke existing pipeline inside `OnParallelTasks` using snapshot | [ ] |
| M3 | Finalization step | Build `PreparedSceneFrame`, partitions, publish | [ ] |
| M4 | Renderer consumption (dual) | Renderer still builds legacy AoS + queries module for parity logs | [ ] |
| M5 | GPU translation path | Create full multi-draw metadata buffer (replacing single entry) | [ ] |
| M6 | First pass migrated | Pick simplest pass to consume SoA directly | [ ] |
| M7 | Remove legacy bridge | Delete `BuildRenderItemsFromScenePrep`, `RenderItemsList` | [ ] |
| M8 | Rename CPU DrawMetadata | Optional clarity rename after parity validated | [ ] |
| M9 | Determinism tests | Snapshot hash + ordering tests | [ ] |
| M10| Perf validation | Compare frame timings pre/post migration | [ ] |

## 9. Risk & Mitigation

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Ordering instability | Flickering or pass mismatch | Stable sort key; unit test hash |
| Memory spike (dual path) | Peak RAM | Keep dual path only during M4–M6; reserve vectors from previous sizes |
| GPU late binding race | Incorrect indices | Perform translation only after resource residency ensured (FrameGraph) |
| Pass mask future changes | Rework needed | Encapsulate mask compute policy behind function in finalization |
| Two DrawMetadata structs confusion | Maintenance errors | Explicit translation function + eventual rename |

## 10. Test Plan (Added Along Migration)

1. Parity: count / material pointer hash / transform matrix hash legacy vs module (M4–M6 only).
2. Determinism: Run same snapshot twice → identical serialized frame (byte compare of arrays).
3. Partition coverage: Sum of partition ranges == total draws; no overlaps; sorted within range.
4. GPU metadata structural: `sizeof(engine::DrawMetadata)` static assert already; add runtime assert counts match CPU logical draws.
5. Performance microbench: capture timings (collection, finalization, translation) across N synthetic frames.

## 11. Open Decisions

| Topic | Question | Default Stance | Due |
|-------|----------|----------------|-----|
| CPU DrawMetadata rename | Rename to `CpuDrawDesc`? | Defer until M7 | After bridge removal |
| Transform dedup | Deduplicate identical matrices? | Not initially (keep 1:1) | Revisit post M7 |
| Sorting policy plug-in | Runtime strategy vs fixed | Start fixed; add policy enum later | Post M9 |
| Indirect draws | Introduce argument buffer generation | Defer | After correctness pass |

## 12. Immediate Next Action (M1)

Add `ScenePrepModule` skeleton (header + cpp) with phase mask and logging stubs; wire registration in engine bootstrap (not yet consuming). No behavioral changes.

---
This document is a living internal progress tracker; update statuses & decisions as milestones advance.

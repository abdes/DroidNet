# ScenePrep (Current Collection Phase)

This README summarizes the actual, shipped state of the ScenePrep subsystem. It
intentionally strips out earlier speculative module design notes that are no
longer representative. For forward-looking plans, see the updated
`Docs/scene_prep.md.md`.

## 1. Current Purpose

ScenePrep converts the scene graph into a Structure-of-Arrays list of
`RenderItemData` – one entry per visible submesh – via a configurable collection
pipeline. These SoA records are temporarily converted into the legacy
`RenderItemsList` (AoS) in `Renderer::BuildFrame` until downstream passes are
updated to consume ScenePrep outputs directly. Implemented scope (Sept 2025):

* Node traversal & eligibility filtering
* LOD selection (Fixed, Distance, Screen-Space Error policies)
* Per-submesh visibility + frustum filtering
* Material override resolution per visible submesh
* Emission of `RenderItemData` (SoA) into `ScenePrepState::collected_items`

Not yet implemented:

* Finalization phase (GPU residency, uploads, draw metadata expansion)
* Sorting / partitioning / pass mask computation
* Direct multi-pass consumption of SoA
* Transform/material/geometry registries with residency + batching

## 2. High-Level Flow

```text
Scene -> (ScenePrepPipelineCollection::Collect) -> RenderItemData[] (SoA)
  -> (temporary bridge in Renderer) -> RenderItemsList (AoS) -> Passes
```

## 3. Implemented Extractors

| Extractor | Responsibility |
|-----------|----------------|
| ExtractionPreFilter | Visibility flag & basic geometry presence; drops invisible nodes. |
| MeshResolver | Selects active LOD (policy-driven) & resolves mesh pointer. |
| SubMeshVisibilityFilter | Computes visible submesh indices (flags + frustum tests). |
| EmitPerVisibleSubmesh | Emits one `RenderItemData` per visible submesh, resolves material (override → submesh → default). |

## 4. Output (Current)

`ScenePrepState::collected_items` (vector of `RenderItemData`). Each element
contains:

* geometry (`shared_ptr<GeometryAsset>`)
* material (`shared_ptr<MaterialAsset>`)
* world_transform (glm::mat4)
* lod_index
* submesh_index
* visibility & shadow flags
* visible_submesh list (for intermediate stages)

## 5. Temporary Bridge

Function: `BuildRenderItemsFromScenePrep` (internal in `Renderer.cpp`).

Responsibility:

* Convert each `RenderItemData` → `RenderItem` (AoS)
* Resolve mesh pointer (`geometry->MeshAt(lod_index)`, guarded) and material
* Copy transform & flags, recompute derived bounds

Removal Criteria:

* Passes updated to accept SoA + draw metadata directly
* Finalization phase provides ordering + partition map

## 6. Usage (Simplified)

```cpp
namespace sp = oxygen::engine::sceneprep;
sp::ScenePrepState prep_state;
auto cfg = sp::CreateBasicCollectionConfig();
sp::ScenePrepPipelineCollection pipeline{cfg};
pipeline.Collect(scene, view, frame_seq.get(), prep_state);
// Temporary conversion inside renderer afterwards
```

## 7. Visibility & LOD Overview

* LOD policy resolved in `MeshResolver` (Fixed | Distance | ScreenSpaceError)
* Submesh culling uses frustum planes + node/submesh visibility masks
* No hierarchical or occlusion culling yet

## 8. Planned Finalization (High-Level)

Will introduce:

* Batched transform/material/geometry upload & caching
* Draw metadata emission (one record per MeshView)
* Sorting & pass partitioning
* Direct pass consumption (eliminate AoS bridge)

## 9. Roadmap (Abbreviated)

| ID | Status | Summary |
|----|--------|---------|
| 15 | ✅ | Renderer uses ScenePrep collection path. |
| 17 | ⏳ | Remove bridge & add SoA consumer passes. |
| 10 | ⏳ | Finalization config + roles. |
| 11 | ⏳ | Draw metadata / MeshView expansion. |
| 12 | ⏳ | Sorting + partition map. |
| 9  | ⏳ | Unified GPU buffer manager. |
| 13 | ⏳ | Perf optimizations & pooling. |
| 18 | ⏳ | Doc refresh after finalization lands. |

## 10. Deferred Features

| Feature | Reason Deferred | Future Hook |
|---------|-----------------|-------------|
| Light masks / influence lists | Requires broader lighting system | Extend `DrawMetadata` + optional light mask vector |
| Material variant hashing | Depends on pipeline/pso cache integration | Additional field or external map keyed by `(mesh, material)` |
| Transparency ordering | Requires per-view depth sorting | Lazy compute in FrameGraph phase on first request |
| LOD selection refinement | Needs distance + importance metrics | Pre-pass before culling or integrated into slice cull |
| Occlusion / portal culling | Needs hierarchical structures & temporal coherence | Strategy interface per slice before accept |

## 11. Metrics (Planned)

Collected in merge stage and optionally exposed for adaptive scheduling:

```cpp
struct ScenePrepMetrics {
 uint32_t considered { 0 };
 uint32_t visible_draws { 0 };
 uint32_t culled { 0 };
 uint32_t opaque { 0 };
 uint32_t alpha_test { 0 };
};
```

## 12. Testing

Current tests cover extractor correctness (visibility filtering, LOD policy,
submesh emission). Additional tests will be added for finalization once
implemented.

## 13. Bridge Retirement Criteria

1. All passes accept SoA + draw metadata.
2. No call sites depend on `RenderItemsList` for per-submesh iteration.
3. Performance parity (or better) confirmed.
4. Tests updated to validate new pass inputs.

## 14. Acceptance Criteria (Finalization Phase)

| Criterion | Description |
|----------|-------------|
| Deterministic | Same snapshot → identical `DrawMetadata` sequence |
| Isolation | No EngineState or GameState mutation during ParallelWork |
| Publication | `GetPreparedSceneFrame()` non-null after PostParallel |
| Correct Classification | Opaque/alpha-test counts match material flags |
| No Deferred Leakage | Light masks & transparency ordering absent |

## 15. Future Extension Hooks

Placeholder extension points (intentionally *not* implemented yet):

* Strategy interface: `ICullingStrategy` selected per view or scene partition
* Incremental caching: reuse unchanged static region results
* Transparency ordering service: triggered lazily by FrameGraph
* GPU-assisted culling: compute shader path writing visibility bitset consumed
 by a thin CPU compaction pass

## 16. Summary

ScenePrep now owns scene-to-drawable collection with a clean, testable set of
extractors. A temporary bridge keeps legacy passes functioning. Next major
milestone is implementing Finalization to remove that bridge and expose a fully
SoA, multi-pass friendly representation directly to the renderer.

# ScenePrep Pipeline

This document describes the ScenePrep system as currently implemented. Both
the Collection and Finalization phases are active and used by the Renderer.

## Current Status

- ✅ **Collection Phase**: Scene graph traversal, LOD selection, visibility filtering, and item emission fully operational
- ✅ **Finalization Phase**: GPU uploads, draw metadata assembly, sorting, and
  partitioning complete
- ✅ **Renderer Integration**: Renderer directly uses ScenePrepPipeline with both phases
- ✅ **Legacy Removal**: Former Extraction module fully removed; no SoA→AoS bridge
- ✅ **Helper Implementation**: TransformUploader, MaterialBinder, GeometryUploader, and DrawMetadataEmitter all implemented

For implementation details, see the respective header files in `src/Oxygen/Renderer/ScenePrep/` and `src/Oxygen/Renderer/Resources/`.

---

## Implementation Task List (Current Status)

| Status | ID | Task | Description |
| ------ | -- | ---- | ----------- |
| ✅ | 1 | Core Types | `ScenePrepContext`, `ScenePrepState`, `PassMask`, basic handles fully implemented. |
| ✅ | 2 | Proto/Data Types | `RenderItemProto`, `RenderItemData` established as canonical (no legacy dependency). |
| ✅ | 3 | Collection Extractors | `ExtractionPreFilter`, `TransformResolveStage`, `MeshResolver`, `SubMeshVisibilityFilter`, `EmitPerVisibleSubmesh` fully operational. |
| ✅ | 4 | Collection Config | `CollectionConfig` template with optional stage detection (`CreateBasicCollectionConfig`). |
| ✅ | 5 | Pipeline Orchestration | `ScenePrepPipeline` base + `ScenePrepPipelineImpl` template. Full Collection + Finalization orchestration complete. |
| ✅ | 6 | Helper State (Transforms) | `TransformUploader` fully implemented with per-frame slot reuse and GPU upload. |
| ✅ | 7 | Helper State (Materials) | `MaterialBinder` fully implemented with deduplication, persistent caching, and bindless access. |
| ✅ | 8 | Helper State (Geometry) | `GeometryUploader` implemented with residency tracking and handle management. |
| ✅ | 9 | Unified GPU Buffer Manager | `TransientStructuredBuffer` and coordinate via `UploadCoordinator`. |
| ✅ | 10 | Finalization Config & Roles | `FinalizationConfig` fully templated with 6 finalization stages (geometry, transform, material, emit, sort, upload). |
| ✅ | 11 | Draw Metadata System | `DrawMetadataEmitter` handles MeshView expansion + per-item `DrawMetadata` emission. |
| ✅ | 12 | Sorting & Partitioning | `DrawMetadataEmitter::SortAndPartition()` implemented for stable ordering. |
| ✅ | 13 | Performance Optimizations | Deduplication (material/geometry), frame-aware slot reuse, batched uploads via `UploadCoordinator`. |
| ✅ | 14 | Expanded Test Coverage | Unit tests cover collection (`Frustum_test`, `Link_test`, `MaterialBinder_test`). Additional integration tests in place. |
| ✅ | 15 | Renderer Integration | `Renderer::BuildFrame()` uses `ScenePrepPipeline::Collect()` and `Finalize()` directly. |
| ✅ | 16 | Legacy Removal | Former Extraction module fully removed from build. |
| ✅ | 17 | Bridge Retirement | SoA→AoS bridge removed; passes now consume ScenePrep outputs directly. |
| ✅ | 18 | Documentation Refresh | This document aligns with current implementation (Dec 2025). |

Legend: ✅ complete, 🔄 in progress, ⏳ pending.

### Status Legend

- ⏳ **Pending**: Task not started
- 🔄 **In Progress**: Task currently being worked on
- ✅ **Complete**: Task finished and tested
- ❌ **Blocked**: Task cannot proceed due to dependencies

### Estimated Timeline

- **Phase 1** (Tasks 1-8): Core Infrastructure - ~2-3 weeks
- **Phase 2** (Tasks 9-14): Implementation and Optimization - ~2-3 weeks
- **Phase 3** (Tasks 15-18): Testing, Integration, and Cleanup - ~1-2 weeks

---

## Overview

ScenePrep is architected as a two-phase system with both phases implemented
and active in the renderer:

### Implemented: Collection Phase

- Scene graph traversal & eligibility filtering via `ExtractionPreFilter`
- LOD selection (distance / screen-space error policies) via `MeshResolver`
- Per-submesh frustum & visibility filtering via `SubMeshVisibilityFilter`
- Emission of one `RenderItemData` per visible submesh (SoA) via `EmitPerVisibleSubmesh`
- Stable transform handle allocation via `TransformResolveStage`

### Implemented: Finalization Phase

- GPU residency checks & batched uploads via `GeometryUploadFinalizer`, `TransformUploadFinalizer`, `MaterialUploadFinalizer`
- Draw metadata expansion (per MeshView) via `DrawMetadataEmitFinalizer`
- Sorting & partitioning via `DrawMetadataSortAndPartitionFinalizer`
- Direct GPU upload of draw metadata via `DrawMetadataUploadFinalizer`
- SoA publication directly to passes (bridge removed; no AoS conversion)

---

## Implemented Collection Extractors

- **ExtractionPreFilter**: Seeds visibility/shadow flags, geometry, and transform handle; drops invisible nodes via effective visibility check.
- **TransformResolveStage**: Allocates deterministic transform handles via
  `TransformUploader::GetOrAllocate()`.
- **MeshResolver**: Selects active LOD (distance/SSE policies) and resolves the mesh. View-dependent; skips in frame-phase.
- **SubMeshVisibilityFilter**: Computes visible submesh indices using node visibility masks and per-submesh frustum culling (AABB preferred, world-sphere fallback).
- **EmitPerVisibleSubmesh**: Emits one `RenderItemData` per visible submesh, resolving material per submesh (override → mesh submesh → default).

---

## Implementation Guide

### Recommended Implementation Order

1. **Start with basic infrastructure**: ScenePrepContext, ScenePrepState, core types
2. **Implement simple filter**: Basic pass mask computation
3. **Add minimal uploaders**: Identity transforms, default materials, resident geometry
4. **Implement assemblers**: Build RenderItem from cached data
5. **Add sorting**: Basic front-to-back or material-based sorting
6. **Optimize uploaders**: Add batching, deduplication, real GPU uploads
7. **Add draw metadata**: Compute mesh view ranges and draw commands

### Example Usage

```cpp
// Example configuration for a basic forward renderer
struct BasicScenePrepConfig {
  auto filter = [](const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item) -> PassMask {
    PassMask mask = 0;
    if (item.domain == MaterialDomain::kOpaque) mask |= kOpaquePass;
    if (item.domain == MaterialDomain::kTransparent) mask |= kTransparentPass;
    if (item.cast_shadows) mask |= kShadowPass;
    return mask;
  };

  auto transform_uploader = [](std::span<const RenderItemData> all_items,
                               std::span<const std::size_t> indices,
                               const ScenePrepContext& ctx, ScenePrepState& state) {
    // Batch upload transforms for items at the given indices
    for (auto idx : indices) {
      const auto& item = all_items[idx];
      // Item already carries a TransformHandle populated during collection.
      state.transform_cache.SetHandle(idx, item.transform_handle);
    }
  };
    struct RenderItemData; struct RenderItem; struct MeshView; struct DrawMetadata;
    struct ScenePrepContext; struct ScenePrepState; using PassMask = uint32_t;
    // Core types are implemented in the ScenePrep module. See:
    // `src/Oxygen/Renderer/ScenePrep/Types.h` (core types: `ScenePrepContext`, `PassMask`, `DrawMetadata`, handles)
    // `src/Oxygen/Renderer/ScenePrep/RenderItemData.h` (collection-phase `RenderItemData`)
    // `src/Oxygen/Renderer/ScenePrep/ScenePrepState.h` (pipeline state and helpers)
    // `src/Oxygen/Renderer/ScenePrep/Concepts.h` (C++20 concepts for filters/uploaders/assemblers)

auto result = ScenePrep(collected_items, ctx, state, config.filter,
                        config.transform_uploader, /*...*/);
```

---

## Future Finalization Phase Goals

When Finalization is implemented it should produce:

- Stable ordering & partition map (per pass / domain)
- Draw metadata (one record per MeshView draw)
- Batched transform/material/geometry uploads with residency tracking
- Compact pass masks enabling multi-pass reuse
- Zero AoS conversion (passes consume SoA directly)

---

## Planned Finalization Algorithms (multi-pass friendly)

The ScenePrep pipeline is a composition of algorithms operating over input items
and explicit context/state. No inheritance is required; state may include
stateful classes like TransformManager.

### Transform Finalization

TransformUploader (Uploader)

- CPU: process items by index to detect dirty transforms and coalesce upload
  ranges; prepare slot mapping without data copies.
- GPU: upload coalesced transform ranges to the transform buffer.
- Output: state.transform_cache updated (ranges, slot map); transform buffer
  updated.

TransformAssembler (Assembler)

- CPU: for each item, attach transform index or resolved matrices.
- Output: per-item transform mapping/materialized matrices.

Fallback/error policy:

- No transform component → use identity matrices.
- Slot exhaustion → temporarily embed matrices this frame or reuse LRU slot.

### Material Finalization

MaterialUploader (Uploader)

- CPU: build/dedupe MaterialConstants array; compute MaterialAsset* → index map.
- GPU: upload material constants; ensure texture residency (bindless handles
  ready).
- Output: state.material_cache updated (constants array, index map); textures
  resident.

MaterialAssembler (Assembler)

- CPU: assign material indices/handles to items; apply defaults for missing
  data.
- Output: per-item material references/indices/handles.

Fallback/error policy:

- Missing material asset → bind default material index and texture handles.
- Failed constants upload → reuse previous-frame index if valid; otherwise
  defaults.

### Geometry Finalization

GeometryUploader (Uploader)

- CPU: resolve geometry views; compute required residency; obtain/register
  bindless buffer handles via GeometryRegistry (idempotent).
- GPU: ensure vertex/index buffer residency (schedule uploads as needed).
- Output: stable GeometryHandle stored/queried via registry (no per-frame cache).

GeometryAssembler (Assembler)

- CPU: resolve mesh pointer = geometry->MeshAt(lod); validate submesh index;
  fetch stable handles from registry (LookupGeometryHandle) or register lazily.
- Output: per-item mesh/submesh and bindless geometry handles.

Fallback/error policy:

- Invalid submesh index → drop item or emit zero-count draw.
- Missing residency/handle → schedule upload for next frame; reuse prior handle
  if valid or drop.

### Item Filter / Routing

- CPU: single-run pipeline must compute per-pass participation without
  re-finalizing.
- Per-item: apply pass/layer/domain rules; set pass-mask (bitset) and drop items
  that participate in no passes.
- GPU: none.
- Output: multi-pass friendly dataset (pass-mask on each item or sidecar table).

Contract:

- Pass mask is a bitset; each bit corresponds to a renderer-defined pass ID.
- Determinism: identical input state must yield identical masks (stable across
  runs).
- Pure CPU: must not trigger resource uploads or GPU queries.

### Sort / Partition

- CPU: compute stable keys (e.g., domain, material, pipeline, mesh) and sort;
  partition for passes/domains where useful.
- GPU: none.
- Output: ordering indices and partitions reused by downstream passes.

Contract:

- Returns stable permutation `order` (length = filtered item count).
- Returns `partitions`: a map pass_id → [begin, end) ranges over `order`.
- Sorting keys derive only from CPU state and cached/bindless indices.

### Flags Assembly

- CPU: compute and attach engine flags/domains/per-item options (non-pass
  routing).
- Output: updated item flags.

### Draw Metadata Assembly

- CPU: for each MeshView, emit one DrawMetadata with ranges
  (first_index/base_vertex or first_vertex/vertex_count) and any required
  bindless indices.
- Output: DrawMetadata arrays consistent with submission order.

DrawMetadata fields (minimum):

- first_index, index_count, base_vertex (indexed draws); or
- first_vertex, vertex_count (non-indexed draws);
- optional: draw_id, mesh/submesh identifiers for debugging/validation.

---

## Concept Interfaces (Implemented)

ScenePrep algorithms are implemented via C++20 concepts and organized by role:

### Collection Concepts

```cpp
template <typename F>
concept RenderItemDataExtractor = requires(
  F f, ScenePrepContext& ctx, ScenePrepState& state, RenderItemProto& item) {
  { f(ctx, state, item) } -> std::same_as<void>;
};
```

- **Per-item processing** during collection
- May mutate `RenderItemProto` and `ScenePrepState`
- May mark item as dropped to skip downstream stages

Implemented collection extractors:

- `ExtractionPreFilter`: Validates visibility and seeds basic data
- `TransformResolveStage`: Allocates stable transform handles
- `MeshResolver`: Selects LOD and resolves mesh
- `SubMeshVisibilityFilter`: Computes visible submesh indices
- `EmitPerVisibleSubmesh`: Emits one `RenderItemData` per visible submesh

### Finalization Concepts

Three key concepts for finalization:

**1. Finalizer** (batch-level operations):

```cpp
template <typename U>
concept Finalizer = requires(U u, ScenePrepState& state) {
  { u(state) } -> std::same_as<void>;
};
```

- Prepares GPU resources and allocates stable handles
- Mutable access to `ScenePrepState`
- Examples: `GeometryUploadFinalizer`, `TransformUploadFinalizer`

**2. Uploader** (batch GPU operations):

```cpp
template <typename U>
concept Uploader = requires(U u, const ScenePrepState& state) {
  { u(state) } -> std::same_as<void>;
};
```

- Uploads prepared data to GPU
- Read-only access to `ScenePrepState`
- Must be idempotent
- Examples: `MaterialUploadFinalizer`, `DrawMetadataUploadFinalizer`

**3. DrawMetadataEmitter** (per-item metadata emission):

```cpp
template <typename E>
concept DrawMetadataEmitter = requires(
  E e, ScenePrepState& state, const RenderItemData& item) {
  { e(state, item) } -> std::same_as<void>;
};
```

- Per-item processing to build metadata
- May mutate `ScenePrepState`
- Example: `DrawMetadataEmitFinalizer`

### Finalization Stages (in order)

1. **GeometryUpload**: Ensure geometry residency
2. **TransformUpload**: Upload transform matrices
3. **MaterialUpload**: Upload material constants
4. **DrawMetadataEmit**: Per-item metadata emission
5. **DrawMetadataSort**: Sort and partition metadata
6. **DrawMetadataUpload**: Upload metadata to GPU

Each stage is optional (use `void` in config to skip).

### Pipeline Architecture

```cpp
template <typename CollectionCfg, typename FinalizationCfg>
class ScenePrepPipelineImpl : public ScenePrepPipeline {
  auto CollectImpl(/*...*/) -> void override;
  auto FinalizeImpl(/*...*/) -> void override;
};
```

- `CollectionCfg`: Specifies optional collection extractors
- `FinalizationCfg`: Specifies optional finalization stages
- `ScenePrepPipeline`: Base class with `Collect()` and `Finalize()` methods
- Extracted from factory functions: `CreateBasicCollectionConfig()`, `CreateStandardFinalizationConfig()`

Notes:

- All algorithms are function objects (free functions, lambdas, or callable types)
- No virtual dispatch; all routing via `if constexpr`
- Zero-cost abstraction; optional stages compile away if not used
- Thread-unsafe; use one instance per rendering thread (shared registries OK)

```cpp
    // Simple partitioning by material domain
    PartitionMap partitions;
    std::size_t opaque_start = 0, transparent_start = 0;

    for (std::size_t i = 0; i < order.size(); ++i) {
      const auto& item = all_items[order[i]];
      if (item.domain == MaterialDomain::kTransparent && transparent_start == 0) {
        partitions[0] = {0, i}; // Opaque partition
        transparent_start = i;
      }
    }
    if (transparent_start > 0) {
      partitions[1] = {transparent_start, order.size()}; // Transparent partition
    } else {
      partitions[0] = {0, order.size()}; // All opaque
    }
    return {std::move(order), std::move(partitions)};
```

---

## Helper State (Implementation Status)

All helper classes are **fully implemented and integrated**. They are owned by `ScenePrepState` via `unique_ptr` to allow optional configurations:

### Transform Management

- **TransformUploader** (persistent, owned by ScenePrepState): Manages stable
  transform handles with per-frame GPU upload.
  - `GetOrAllocate(const glm::mat4&)` → `TransformHandle`: assigns indices in
    call order and reuses slots per frame for deterministic handles.
  - `EnsureFrameResources()`: uploads world and normal matrices to GPU via
    transient buffers.
  - Normal matrices (inverse-transpose) computed automatically.
  - Accessed via `ScenePrepState::GetTransformUploader()`.
  - Located in `src/Oxygen/Renderer/Resources/TransformUploader.h/cpp`.

### Material Management

- **MaterialBinder** (persistent, owned by ScenePrepState): Manages material
  constants with deduplication and atlas allocation.
  - `GetOrAllocate(...)` → `MaterialHandle`: registers unique materials with
    content-based hashing.
  - `EnsureFrameResources()`: uploads material constants and resolves texture
    residency via TextureBinder.
  - Integrates with `UploadCoordinator` for efficient batch uploads.
  - Accessed via `ScenePrepState::GetMaterialBinder()`.
  - Located in `src/Oxygen/Renderer/Resources/MaterialBinder.h/cpp`.

### Geometry Management

- **GeometryUploader** (persistent, owned by ScenePrepState): Manages geometry residency and bindless handles.
  - `EnsureFrameResources()`: Ensures vertex/index buffer residency (GPU-side).
  - Idempotent handle allocation for registered geometries.
  - Integrates with bindless resource system for shader access.
  - Accessed via `ScenePrepState::GetGeometryUploader()`.
  - Located in `src/Oxygen/Renderer/Resources/GeometryUploader.h/cpp`.

### Draw Metadata Management

- **DrawMetadataEmitter** (persistent, owned by ScenePrepState): Generates draw metadata from render items.
  - `EmitDrawMetadata(const RenderItemData&)`: Per-item processing to build metadata.
  - `SortAndPartition()`: Stable ordering and partition map generation.
  - `EnsureFrameResources()`: GPU upload of metadata for bindless access.
  - Accessed via `ScenePrepState::GetDrawMetadataEmitter()`.
  - Located in `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.h/cpp`.

### Ownership Model

```cpp
struct ScenePrepState {
  std::unique_ptr<renderer::resources::GeometryUploader> geometry_uploader_;
  std::unique_ptr<renderer::resources::TransformUploader> transform_mgr_;
  std::unique_ptr<renderer::resources::MaterialBinder> material_binder_;
  std::unique_ptr<renderer::resources::DrawMetadataEmitter> draw_emitter_;
  // ... plus retained items tracking, logging, etc.
};
```

Helpers are optional (may be null) to allow:

- Flexible configuration (different pipeline stages)
- GPU-independent unit testing
- Partial implementations for debugging

### Frame Lifecycle

Each helper implements frame management:

1. **OnFrameStart()**: Called at frame boundary (once per frame per view).
   - Resets per-frame state (allocations, sorting, etc.).
   - Allocates transient GPU buffers for this frame's data.
   - Preps cached data for new frame.

2. **Collection Phase**: Extractors invoke helpers to allocate handles.
   - `TransformResolveStage` calls `TransformUploader::GetOrAllocate()`.
   - `EmitPerVisibleSubmesh` resolves materials and geometry.
   - Handles are stored in `RenderItemData` for later use.

3. **Finalization Phase**: Finalizers invoke `EnsureFrameResources()` to upload accumulated data.
   - `TransformUploadFinalizer` → uploads per-frame transforms.
   - `MaterialUploadFinalizer` → uploads constants and ensures residency.
   - `GeometryUploadFinalizer` → ensures VB/IB residency.
   - `DrawMetadataEmitFinalizer` → emits metadata per item.
   - `DrawMetadataSortAndPartitionFinalizer` → sorts and partitions.
   - `DrawMetadataUploadFinalizer` → uploads metadata to GPU.

4. **Rendering**: Shaders access handles via bindless tables (SRV indices from helpers).

---

## Testing Strategy (Current & Planned)

### Unit Testing Individual ScenePrep Algorithms

Each ScenePrep algorithm can be tested in isolation using mock data:

```cpp
TEST(ScenePrepTest, BasicPassFilterWorks) {
  // Setup
  RenderItemData item;
  item.domain = MaterialDomain::kOpaque;
  item.cast_shadows = true;

  ScenePrepContext ctx;
  ScenePrepState state;
  BasicPassFilter filter;

  // Execute
  auto mask = filter(item, ctx, state);

  // Verify
  EXPECT_EQ(mask & (1u << 0), 1u << 0); // Opaque pass
  EXPECT_EQ(mask & (1u << 2), 1u << 2); // Shadow pass
}

TEST(ScenePrepTest, TransformUploaderBatches) {
  // Setup mock items with same transform
  std::vector<RenderItemData> items(100);
  glm::mat4 shared_transform = glm::mat4(1.0f);
  for (auto& item : items) {
  // Item transform now deduplicated via TransformManager; handle assigned during collection
  }

  std::vector<std::size_t> indices(100);
  std::iota(indices.begin(), indices.end(), 0);

  MockScenePrepState state;
  BasicTransformUploader uploader;

  // Execute
  uploader(items, indices, ctx, state);

  // Verify deduplication occurred
  EXPECT_EQ(state.transform_mgr.GetUniqueTransformCount(), 1);
}
```

### Integration Testing

Test complete ScenePrep pipeline with representative data:

```cpp
TEST(ScenePrepTest, EndToEndPipeline) {
  // Setup scene with known objects
  auto collected_items = CreateTestScene(/*obj_count=*/50);

  // Configure ScenePrep algorithms
  auto config = CreateBasicScenePrepConfig();

  // Execute
  auto result = ScenePrep(collected_items, ctx, state, config);

  // Verify outputs
  EXPECT_FALSE(result.items.empty());
  EXPECT_EQ(result.items.size(), result.draw_metadata.size());
  // Verify sorting order is correct
  // Verify pass masks are assigned
  // Verify all GPU resources are valid
}

TEST(ScenePrepTest, FlexibleConfigurationSkipsStages) {
  // Test GPU-independent configuration with only essential stages
  auto collection_config = CreateMockCollectionConfig(); // Only filter + producer
  auto finalization_config = CreateMockScenePrepConfig(); // Only filter + assembler

  auto unified_config = ScenePrepConfig{
    .collection = std::move(collection_config),
    .finalization = std::move(finalization_config)
  };

  ScenePrepPipeline pipeline{std::move(unified_config)};
  ScenePrepState state;

  // Mock scene and context (no GPU dependencies)
  MockScene scene;
  MockView view;
  MockRenderContext render_context;

  // Execute - should skip uploaders, material extractors, etc.
  auto result = pipeline.ScenePrep(scene, view, /*frame_id=*/1, render_context, state);

  // Verify minimal pipeline worked
  EXPECT_FALSE(result.items.empty());
  EXPECT_EQ(result.collected_count, scene.GetObjectCount());
  EXPECT_EQ(result.filtered_count, result.items.size()); // No filtering in mock

  // Verify stages were skipped gracefully
  EXPECT_TRUE(state.transform_cache.IsEmpty()); // Transform uploader was skipped
  // material_cache removed; no per-frame material cache assertions
}

TEST(ScenePrepTest, PartialConfigurationWorks) {
  // Create config with only some stages to test graceful degradation
  auto partial_filter = [](const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item) -> PassMask {
    return 1u; // Single pass
  };

  // Only provide filter - all other stages will be skipped with if constexpr
  auto partial_config = ScenePrepConfig<decltype(partial_filter)>{
    .filter = std::move(partial_filter)
  };

  static_assert(partial_config.has_filter == true);
  static_assert(partial_config.has_transform_uploader == false);
  static_assert(partial_config.has_material_uploader == false);
  static_assert(partial_config.has_geometry_uploader == false);

  // Should compile and run successfully, using defaults for missing stages
  std::vector<RenderItemData> mock_items = CreateMockRenderItems(10);
  ScenePrepContext ctx{};
  ScenePrepState state{};

  // This would call ScenePrep with only filter stage
  // All other stages gracefully skipped via if constexpr
}
```

## Performance Considerations (Targets)

### Expected Performance Characteristics

- **Target**: Process 10,000 items in < 2ms on modern hardware
- **Memory**: Temporary allocations should not exceed 16MB per frame
- **GPU Uploads**: Batch uploads to minimize API calls (target: < 10 uploads per frame)
- **Cache Efficiency**: Process items in index order to maintain cache locality

### Memory Layout Guidelines

```cpp
// Good: Structure of Arrays for better cache performance
struct TransformBatchCache {
  std::vector<TransformHandle> handles;     // Indexed by original item index
  std::vector<glm::mat4> uploaded_matrices; // Densely packed for upload
  std::vector<std::size_t> dirty_indices;   // Items needing re-upload
};

// Avoid: Array of Structures (poor cache performance for large batches)
struct BadTransformCache {
  struct Entry {
    TransformHandle handle;
    glm::mat4 matrix;
    bool dirty;
    std::size_t item_index;
  };
  std::vector<Entry> entries; // Cache-unfriendly for bulk operations
};
```

### Optimization Checklist

- [ ] Avoid dynamic memory allocation during finalization (use pre-allocated buffers)
- [ ] Process items by index to maintain input data cache locality
- [ ] Batch GPU uploads to reduce API overhead
- [ ] Use stable sorting to maintain deterministic output
- [ ] Reuse transform slots deterministically; deduplicate materials and geometry handles
- [ ] Profile with representative scene data (1K, 10K, 100K items)

---

## Finalization Pipeline (Implemented)

The complete finalization pipeline is orchestrated in `ScenePrepPipelineImpl::FinalizeImpl()`:

```cpp
// Pseudocode overview (actual code uses if constexpr dispatching)
void Finalize(ScenePrepState& state) {
  // 1. Emit draw metadata per-item
  for (const auto& item : state.RetainedItems()) {
    if constexpr (FinalizationCfg::has_draw_md_emit) {
      finalization_.draw_md_emit(state, item);
    }
  }

  // 2. Sort and partition
  if constexpr (FinalizationCfg::has_draw_md_sorter) {
    finalization_.draw_md_sort(state);
  }

  // 3. Batch GPU uploads (no per-item iteration)
  if constexpr (FinalizationCfg::has_geometry_upload) {
    finalization_.geometry_upload(state);
  }
  if constexpr (FinalizationCfg::has_transform_upload) {
    finalization_.transform_upload(state);
  }
  if constexpr (FinalizationCfg::has_material_upload) {
    finalization_.material_upload(state);
  }

  // 4. Upload metadata to GPU
  if constexpr (FinalizationCfg::has_draw_md_upload) {
    finalization_.draw_md_upload(state);
  }
}
```

Key design points:

- **Zero-copy**: Indices and handles stored in `RenderItemData` avoid data copies
- **Determinism**: Sorting produces stable ordering; no random behavior
- **Composition**: Each stage is independent; ordering and dispatch via template parameters
- **Efficiency**: Batch uploads with UploadCoordinator; deduplication via persistent helpers

---

## Renderer Integration (Current)

The `Renderer` class directly integrates ScenePrep:

### Initialization

```cpp
// In Renderer::Renderer()
scene_prep_ = std::make_unique<sceneprep::ScenePrepPipelineImpl<
    decltype(sceneprep::CreateBasicCollectionConfig()),
    decltype(sceneprep::CreateStandardFinalizationConfig())>>(
  sceneprep::CreateBasicCollectionConfig(),
  sceneprep::CreateStandardFinalizationConfig());

// Create shared state
prep_state_ = std::make_unique<sceneprep::ScenePrepState>(
  std::move(geom_uploader),
  std::move(transform_mgr),
  std::move(material_binder),
  std::move(draw_emitter));
```

### Per-Frame Usage

```cpp
// In Renderer::BuildFrame(const View& view, ...)

// 1. Frame start for all helpers
prep_state_->GetTransformUploader()->OnFrameStart(/*...*/);
prep_state_->GetMaterialBinder()->OnFrameStart(/*...*/);
prep_state_->GetGeometryUploader()->OnFrameStart(/*...*/);
prep_state_->GetDrawMetadataEmitter()->OnFrameStart(/*...*/);

// 2. Traverse scene and collect items
scene_prep_->Collect(scene, resolved_view, frame_id, *prep_state_, true);

// 3. Finalize (uploads and metadata emission)
scene_prep_->Finalize();

// 4. Access draw metadata for rendering
const auto& draw_metadata = prep_state_->GetDrawMetadataEmitter()->GetDrawMetadata();
const auto& partitions = prep_state_->GetDrawMetadataEmitter()->GetPartitions();

// 5. Submit to render passes
for (auto pass_id : enabled_passes) {
  auto [begin, end] = partitions[pass_id];
  for (size_t i = begin; i < end; ++i) {
    const auto& meta = draw_metadata[i];
    // Submit draw command
  }
}
```

### Frame Phase vs. View Phase

- **Frame Phase**: `Renderer::BuildFrame()` calls `Collect()` without a view (frame-based preparation)
- **View Phase**: Per-view collection with camera frustum for LOD selection and culling
- **Finalization**: Deferred until all views/phases complete; single finalization per frame

### Key Properties

- Thread-safe helpers (registries): Each helper is accessed safely from single-threaded Renderer context
- Persistent caches: Transform/Material/Geometry registries persist across frames for stability
- Zero-copy design: `RenderItemData` stores handles; no AoS conversion
- Integrated uploads: `UploadCoordinator` batches uploads automatically

---

## Future Enhancements

### Error Handling with std::expected

Consider replacing the current "fallback to defaults" strategy with proper error propagation via `std::expected<T, E>` for improved debugging and error recovery in production scenarios.

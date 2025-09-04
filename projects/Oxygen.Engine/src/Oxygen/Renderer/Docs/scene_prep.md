# ScenePrep Pipeline

This document describes the current ScenePrep system which (as of Sept 2025)
implements the **Collection phase** of scene preparation and feeds existing
render passes through a temporary conversion bridge. The planned **Finalization
phase** (GPU uploads, draw metadata assembly, sorting / partitioning) is
specified here for forward work but not yet implemented.

Legacy "Extraction" code has been fully removed; ScenePrep is now the canonical
path. Any mention of copying or migrating from the Extraction module has been
eliminated. Remaining legacy render passes still consume an `RenderItemsList`
produced through a temporary SoA→AoS bridge inside `Renderer::BuildFrame`.

---

## Implementation Task List (Revised)

| Status | ID | Task | Description |
|--------|----|------|-------------|
| ✅ | 1 | Core Types | `ScenePrepContext`, `ScenePrepState`, `PassMask`, basic handles implemented. |
| ✅ | 2 | Proto/Data Types | `RenderItemProto`, `RenderItemData` established as canonical (no legacy dependency). |
| ✅ | 3 | Collection Extractors | `ExtractionPreFilter`, `MeshResolver`, `SubMeshVisibilityFilter`, `EmitPerVisibleSubmesh` operational. |
| ✅ | 4 | Collection Config | `CollectionConfig` template with stage detection (`CreateBasicCollectionConfig`). |
| 🔄 | 5 | Pipeline Orchestration | `ScenePrepPipelineCollection` (collection only). Finalization orchestration pending. |
| 🔄 | 6 | Helper State (Transforms) | Transform helper scaffolding present (manager/cache). GPU upload integration TBD. |
| ⏳ | 7 | Helper State (Materials) | Material registry + per-frame cache (design present; implementation minimal). |
| ⏳ | 8 | Helper State (Geometry) | Geometry residency + handles (design present; partial). |
| ⏳ | 9 | Unified GPU Buffer Manager | Shared growth / residency / upload strategy. |
| ⏳ | 10 | Finalization Config & Roles | `FinalizationConfig`, uploader/assembler/filter roles implementation. |
| ⏳ | 11 | Draw Metadata System | MeshView expansion + `DrawMetadata` emission (currently still handled in passes). |
| ⏳ | 12 | Sorting & Partitioning | Stable sort + partition map generation. |
| ⏳ | 13 | Performance Optimizations | Batching, dedupe, pooling, temporal reuse. |
| ⏳ | 14 | Expanded Test Coverage | More unit tests (current tests cover collection only). |
| ✅ | 15 | Renderer Integration | Renderer uses ScenePrep (collection) with SoA→AoS bridge. |
| ✅ | 16 | Legacy Removal | Former Extraction module fully removed from build. |
| ⏳ | 17 | Bridge Retirement | Remove temporary SoA→AoS conversion once passes consume ScenePrep directly. |
| ⏳ | 18 | Documentation Refresh | Align remaining renderer docs with ScenePrep terminology. |

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

ScenePrep is architected as a two-phase system, but currently only the
**Collection Phase** is implemented:

### Implemented: Collection Phase

- Scene graph traversal & eligibility filtering
- LOD selection (distance / screen-space error policies)
- Per-submesh frustum & visibility filtering
- Emission of one `RenderItemData` per visible submesh (SoA)

### Planned: Finalization Phase (not yet implemented)

- GPU residency checks & batched uploads (transforms, materials, geometry)
- Draw metadata expansion (per MeshView) and ordering
- Pass mask computation, sorting & partitioning
- Direct publication of SoA / draw metadata to passes (eliminating AoS list)

### Temporary Bridge

Collected `RenderItemData` is converted to legacy AoS `RenderItemsList` inside
`Renderer::BuildFrame` (`BuildRenderItemsFromScenePrep`) strictly to preserve
existing pass code. This bridge is tracked by Task 17 and will be removed when
passes accept ScenePrep outputs directly.

---

## Implemented collection extractors

- ExtractionPreFilter: Seeds visibility/shadow flags, world transform, and geometry; drops invisible nodes.
- MeshResolver: Selects active LOD (distance/SSE policies) and resolves the mesh.
- SubMeshVisibilityFilter: Computes visible submesh indices using node visibility masks and per-submesh frustum culling (AABB preferred, world-sphere fallback).
- EmitPerVisibleSubmesh: Emits one `RenderItemData` per visible submesh, resolving material per submesh (override → mesh submesh → default).

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
      auto handle = state.transform_mgr.GetOrCreateHandle(item.world_transform);
      state.transform_cache.SetHandle(idx, handle);
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

- CPU: resolve geometry views; compute required residency; determine bindless
  handles.
- GPU: ensure vertex/index buffer residency (schedule uploads as needed).
- Output: state.geometry_cache updated (handles, residency records).

GeometryAssembler (Assembler)

- CPU: resolve mesh pointer = geometry->MeshAt(lod); validate submesh index;
  attach handles.
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

## Concept Interfaces (Planned)

ScenePrep algorithms are grouped by role. A single algorithm can satisfy multiple roles by
providing multiple functions. The orchestrator wires them by concept.

- Filter: decides per-item participation and/or pass routing. CPU-only.
- Uploader: performs batch GPU uploads/prep for a set of items. Batch-only.
- Updater: CPU-side state mutation or sidecar computation (order/partitions,
  flags); may be batch-level and/or per-item.
- Assembler: builds CPU draw snapshot (RenderItem) and per-draw metadata.

GPU work principle: Only Uploader roles perform GPU uploads (batch-only).
Filter, Updater, and Assembler roles are CPU-only.

Role mapping of the built-in ScenePrep algorithms (single-responsibility algorithms):

- Item filter / routing: Filter
- TransformUploader: Uploader
- TransformAssembler: Assembler (may use TransformManager state)
- MaterialUploader: Uploader
- MaterialAssembler: Assembler
- GeometryUploader: Uploader
- GeometryAssembler: Assembler
- Sort / partition: Updater (batch-level; produces order/partitions)
- FlagsAssembler: Assembler
- Draw metadata maker: DrawMetadataMaker

Notes:

- Algorithms may implement multiple interfaces; the orchestrator invokes each
  role at the appropriate phase (filter → uploaders → updaters → assemble).
- Updaters are optional and can be both batch-level and per-item; if present,
  batch updaters run before sorting; item updaters run during assembly.
- Assembler role pairs with DrawMetadataMaker to emit mesh-view ranges.

### Example Patterns (Illustrative / Not Yet Wired)

Here are minimal working examples of each ScenePrep algorithm type to guide implementation:

```cpp
// Example Filter: Basic pass assignment
struct BasicPassFilter {
  auto operator()(const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item) -> PassMask {
    PassMask mask = 0;
    switch (item.domain) {
      case MaterialDomain::kOpaque: mask |= (1u << 0); break;      // Opaque pass
      case MaterialDomain::kTransparent: mask |= (1u << 1); break; // Transparent pass
      case MaterialDomain::kCutout: mask |= (1u << 0); break;     // Same as opaque
    }
    if (item.cast_shadows) mask |= (1u << 2); // Shadow pass
    return mask;
  }
};

// Example Uploader: Transform batching
struct BasicTransformUploader {
  auto operator()(std::span<const RenderItemData> all_items,
                  std::span<const std::size_t> indices,
                  const ScenePrepContext& ctx, ScenePrepState& state) -> void {
    // Collect unique transforms and assign indices
    for (auto idx : indices) {
      const auto& transform = all_items[idx].world_transform;
      auto handle = state.transform_mgr.GetOrAllocate(transform);
      state.transform_cache.MapItemToHandle(idx, handle);
    }
    // Batch upload to GPU happens in TransformManager
    state.transform_mgr.FlushPendingUploads();
  }
};

// Example Assembler: Build final RenderItem
struct BasicGeometryAssembler {
  auto operator()(const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item,
                  const ScenePrepState& state, RenderItem& out) -> void {
    // Resolve mesh pointer from geometry asset
    if (item.geometry) {
      try {
        out.mesh = item.geometry->MeshAt(item.lod_index);
      } catch (...) {
        out.mesh = nullptr; // Will be filtered out later
      }
    }
    out.submesh_index = item.submesh_index;

    // Get cached geometry handles from uploader phase
    auto geom_handle = state.geometry_cache.GetHandle(item.geometry.get());
    out.vertex_buffer_handle = geom_handle.vertex_buffer;
    out.index_buffer_handle = geom_handle.index_buffer;
  }
};

// Example Sort: Front-to-back for opaque objects
struct BasicDepthSort {
  auto operator()(std::span<const RenderItemData> all_items,
                  std::span<const std::size_t> indices,
                  const FinalizeContext& ctx, FinalizeState& state) -> std::pair<std::vector<std::size_t>, PartitionMap> {
    auto order = std::vector<std::size_t>(indices.begin(), indices.end());

    // Sort by depth (front to back for opaque)
    const auto cam_pos = ctx.view.CameraPosition();
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      const auto& item_a = all_items[a];
      const auto& item_b = all_items[b];

      const auto center_a = glm::vec3(item_a.world_bounding_sphere);
      const auto center_b = glm::vec3(item_b.world_bounding_sphere);

      const auto dist_a = glm::distance2(cam_pos, center_a);
      const auto dist_b = glm::distance2(cam_pos, center_b);

      return dist_a < dist_b; // Front to back
    });

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
  }
};
```

---

## Helper State (Design & Partial Implementation)

Single-responsibility algorithms that participate in multiple phases (e.g.,
Uploader then Assembler) must not hide cross-stage data in static/thread-local
state. Persist it explicitly in the pipeline state:

- Ownership: ScenePrepState owns helper state via unique_ptr (one per subsystem).
- Lifetime: spans the entire ScenePrep() call (frame/view), then reused across
  frames if appropriate (e.g., TransformManager persistent slot cache).
- Access: algorithms receive references to these helpers via ScenePrepState.
- Contract: Uploaders write batch products; Assemblers read them only.

Examples:

- Transform
  - Helper: TransformManager (persistent), plus per-frame TransformBatchCache
  - Uploader writes coalesced upload ranges and slot mappings into cache
  - Assembler reads slot indices or resolved matrices

- Material
  - Helper: MaterialUploadCache (per-frame), MaterialRegistry (persistent)
  - Uploader builds deduped constants array and uploads; records index map
  - Assembler reads material index and texture handles

- Geometry
  - Helper: GeometryResidencyCache (per-frame), GeometryRegistry (persistent)
  - Uploader ensures VB/IB residency and records bindless handles
  - Assembler resolves mesh pointers and submesh validation using recorded data

These helpers are plain classes held as direct members of ScenePrepState to avoid
pointer indirection and improve cache performance:

```cpp
struct ScenePrepState {
  TransformManager transform_mgr;      // persistent
  TransformBatchCache transform_cache; // per-frame
  MaterialRegistry material_registry;  // persistent
  MaterialUploadCache material_cache;  // per-frame
  GeometryRegistry geometry_registry;  // persistent
  GeometryResidencyCache geometry_cache; // per-frame
  // ... plus logging, etc.
};
```

### Helper Class Specifications

```cpp
// Persistent transform management with GPU buffer allocation
class TransformManager {
public:
  auto GetOrAllocate(const glm::mat4& transform) -> TransformHandle;
  auto FlushPendingUploads() -> void;
  auto GetUniqueTransformCount() const -> std::size_t;

private:
  std::unordered_map<glm::mat4, TransformHandle> transform_to_handle_;
  std::vector<glm::mat4> pending_uploads_;
  TransformHandle next_handle_{0};
};

// Per-frame cache mapping item indices to transform handles
class TransformBatchCache {
public:
  auto MapItemToHandle(std::size_t item_idx, TransformHandle handle) -> void;
  auto GetHandle(std::size_t item_idx) const -> std::optional<TransformHandle>;
  auto Reset() -> void; // Called at start of each frame

private:
  std::vector<TransformHandle> item_to_handle_; // Indexed by item index
};

// Persistent material registry with deduplication
class MaterialRegistry {
public:
  auto RegisterMaterial(std::shared_ptr<const MaterialAsset> material) -> MaterialHandle;
  auto GetHandle(const MaterialAsset* material) const -> std::optional<MaterialHandle>;

private:
  std::unordered_map<const MaterialAsset*, MaterialHandle> material_to_handle_;
  MaterialHandle next_handle_{0};
};

// Per-frame material upload cache
class MaterialUploadCache {
public:
  auto RecordMaterialIndex(std::size_t item_idx, MaterialHandle handle) -> void;
  auto GetMaterialHandle(std::size_t item_idx) const -> std::optional<MaterialHandle>;
  auto Reset() -> void;

private:
  std::vector<MaterialHandle> item_to_material_; // Indexed by item index
};

// Persistent geometry registry with residency tracking
class GeometryRegistry {
public:
  auto EnsureResident(const GeometryAsset* geometry) -> GeometryHandle;
  auto IsResident(const GeometryAsset* geometry) const -> bool;

private:
  std::unordered_map<const GeometryAsset*, GeometryHandle> geometry_to_handle_;
};

// Per-frame geometry cache
class GeometryResidencyCache {
public:
  auto SetHandle(const GeometryAsset* geometry, GeometryHandle handle) -> void;
  auto GetHandle(const GeometryAsset* geometry) const -> std::optional<GeometryHandle>;
  auto Reset() -> void;

private:
  std::unordered_map<const GeometryAsset*, GeometryHandle> geometry_handles_;
};

// Handle types used throughout the system
using TransformHandle = std::uint32_t;
using MaterialHandle = std::uint32_t;
struct GeometryHandle {
  std::uint32_t vertex_buffer;
  std::uint32_t index_buffer;
};

// Partition map for organizing items by render pass
using PartitionMap = std::unordered_map<std::uint32_t, std::pair<std::size_t, std::size_t>>;
```

Keep each finalizer algorithm focused on a single role; data flow happens via
these helpers or the global state where it truly belongs.

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
    item.world_transform = shared_transform;
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
  EXPECT_TRUE(state.material_cache.IsEmpty());  // Material uploader was skipped
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
- [ ] Deduplicate transforms, materials, and geometry handles
- [ ] Profile with representative scene data (1K, 10K, 100K items)

---

## Pseudocode: Future Full (Collection + Finalization) Pipeline

Note: ScenePrep intentionally does not reuse the tuple-based
`Extraction::Pipeline`. Filter and assembly are invoked directly to keep phase
boundaries explicit and avoid indirection where only single-role algorithms are
needed.

```text
function ScenePrep(collected_items, ctx, state, outputs)
  // 0) Filter first (CPU-only) with direct invocation
  filtered_indices = []
  pass_mask = []  // sidecar bitset aligned with filtered_indices
  for idx in range(0, collected_items.size):
    d = collected_items[idx]
    m = Filter(d, ctx, state)
    if m != 0:
      filtered_indices.push_back(idx)
      pass_mask.push_back(m)

  // 1) Batch preparation on filtered set (GPU uploads happen here)
  // Process items by index to avoid data copies
  TransformUploader(collected_items, filtered_indices, ctx, state)  // [GPU] -> writes state.transform_cache
  MaterialUploader(collected_items, filtered_indices, ctx, state)   // [GPU] -> writes state.material_cache
  GeometryUploader(collected_items, filtered_indices, ctx, state)   // [GPU] -> writes state.geometry_cache

  // 2) Sorting/partitioning once (CPU) - work with indices directly
  order, partitions = SortPartition(collected_items, filtered_indices, ctx, state)
  // Determinism: SortPartition must be stable given equal inputs; avoid frame-time randomness

  // 3) Assemble and emit in final order (CPU) with direct invocation
  outputs.items.clear()
  outputs.items.reserve(filtered_indices.size)
  outputs.draw_metadata.clear()

  for k in order:
    idx = filtered_indices[k]
    d = collected_items[idx]
    item = RenderItem{}
    GeometryAssembler(d, ctx, state, item)   // reads state.geometry_cache/registry
    MaterialAssembler(d, ctx, state, item)   // reads state.material_cache/registry
    TransformAssembler(d, ctx, state, item)  // reads state.transform_cache/manager
    FlagsAssembler(d, ctx, state, item)      // pure CPU
    item.pass_mask = pass_mask[k]
    outputs.items.add(item)

    // Expand to mesh-view draws with per-view ranges
    for view in MeshViews(item.mesh, item.submesh_index):
      meta = DrawMetadataMaker(view, ctx, state)
      outputs.draw_metadata.add(meta)

  // Optional: persist partitions for pass iteration
  outputs.partitions = partitions
```

---

## Future Enhancements

The following enhancements can be added to improve the ScenePrep system
further:

### Error Handling with std::expected

Replace the current "fallback to defaults" strategy with proper error
propagation:

```cpp
enum class ScenePrepError {
  kTransformUploadFailed,
  kMaterialUploadFailed,
  kGeometryUploadFailed,
  kInsufficientMemory,
  kInvalidItemData
};

template<typename... ScenePrepAlgorithms>
auto ScenePrep(std::span<const RenderItemData> items,
               const ScenePrepContext& ctx, ScenePrepState& state,
               ScenePrepAlgorithms&&... algorithms)
  -> std::expected<RenderItemsList, ScenePrepError>;
```

This allows callers to handle specific error conditions rather than silently
falling back to defaults, improving debugging and error recovery.

### Memory Pool Allocator

Add frame-based memory pools to eliminate dynamic allocations during ScenePrep:

```cpp
class FrameAllocator {
  static constexpr std::size_t kPoolSize = 2 * 1024 * 1024; // 2MB
  alignas(std::max_align_t) std::byte pool_[kPoolSize];
  std::size_t offset_ = 0;

public:
  template<typename T>
  auto Allocate(std::size_t count) -> std::span<T> {
    const auto size = sizeof(T) * count;
    const auto aligned_size = (size + alignof(T) - 1) & ~(alignof(T) - 1);
    if (offset_ + aligned_size > kPoolSize) {
      throw std::bad_alloc{};
    }
    auto* ptr = reinterpret_cast<T*>(pool_ + offset_);
    offset_ += aligned_size;
    return std::span<T>{ptr, count};
  }

  auto Reset() noexcept -> void { offset_ = 0; }
};

// Add to ScenePrepState:
struct ScenePrepState {
  // ... existing members ...
  FrameAllocator allocator; // memory pool for temporary allocations
};
```

This optimization eliminates malloc/free overhead during ScenePrep and provides better cache locality for temporary data structures.

### Temporal Coherency Optimization

Track frame-to-frame changes to avoid redundant work:

```cpp
class TemporalCache {
  std::unordered_map<EntityId, CachedItemData> previous_frame_;

public:
  auto HasChanged(EntityId id, const RenderItemData& current) -> bool;
  auto GetCachedTransformIndex(EntityId id) -> std::optional<TransformIndex>;
  auto UpdateCache(EntityId id, const CachedItemData& data) -> void;
  auto EvictStaleEntries(std::uint64_t current_frame, std::uint32_t max_age) -> void;
};

struct CachedItemData {
  glm::mat4 world_transform;
  MaterialId material_id;
  GeometryId geometry_id;
  std::uint64_t last_frame;
  TransformIndex transform_index; // Cached GPU resource index
};
```

This optimization can significantly reduce GPU uploads for static objects and
improve performance in scenes with many persistent objects.

### Multi-Threading Support

Optional parallel processing for large item counts:

```cpp
template<FinalizerUploader U>
void ParallelUpload(std::span<const RenderItemData> all_items,
                    std::span<const std::size_t> indices,
                    U uploader, const FinalizeContext& ctx, FinalizeState& state) {
  static constexpr std::size_t kMinItemsForThreading = 1000;

  if (indices.size() < kMinItemsForThreading) {
  uploader(ctx, state, all_items, indices); // Single-threaded
  } else {
    // Split indices into batches and process in parallel
    auto batches = SplitIndicesIntoBatches(indices, std::thread::hardware_concurrency());
    std::for_each(std::execution::par_unseq,
                  batches.begin(), batches.end(),
                  [&](auto batch_indices) {
                    uploader(all_items, batch_indices, ctx, state);
                  });
  }
}
```

Multi-threading should be used judiciously, as the overhead may not be justified
for smaller scenes. The temporal cache can help identify which items need
processing, making parallel work more effective.

---

## Unified Orchestration (Planned)

Earlier sections referenced refactoring an older builder. That legacy path has
been removed. This section now only captures the intended unified design for
*future* Collection + Finalization orchestration.

### Current Status

- Collection orchestrator exists (`ScenePrepPipelineCollection`).
- Finalization orchestrator not yet implemented.
- No tuple-based pipelines remain; design is already concept-oriented.

### Proposed Unified Architecture (Forward Looking)

```cpp
namespace oxygen::engine::renderer::sceneprep {

//! Unified ScenePrep configuration using consistent concept-based approach
template<typename CollectionConfig, typename FinalizationConfig>
struct ScenePrepConfig {
  CollectionConfig collection;
  FinalizationConfig finalization;
};

//! Unified context shared between Collection and Finalization
struct ScenePrepContext {
  const View& view;
  scene::Scene& scene;
  std::uint64_t frame_id;
  RenderContext& render_context;
};

//! Persistent state spanning both Collection and Finalization phases
struct ScenePrepState {
  // Collection state (reused for finalization)
  std::vector<RenderItemData> collected_items;
  std::vector<std::size_t> filtered_indices;
  std::vector<PassMask> pass_masks;

  // Finalization state (helper classes)
  TransformManager transform_mgr;
  TransformBatchCache transform_cache;
  MaterialRegistry material_registry;
  MaterialUploadCache material_cache;
  GeometryRegistry geometry_registry;
  GeometryResidencyCache geometry_cache;

  // Reset per-frame data
  auto ResetFrameData() -> void {
    collected_items.clear();
    filtered_indices.clear();
    pass_masks.clear();
    transform_cache.Reset();
    material_cache.Reset();
    geometry_cache.Reset();
  }
};

//! Unified ScenePrep result
struct ScenePrepResult {
  RenderItemsList items;
  std::vector<DrawMetadata> draw_metadata;
  PartitionMap partitions;
  std::size_t collected_count;  // For statistics
  std::size_t filtered_count;   // For statistics
};
}
```

### Collection Concepts (Consistent with Finalization)

```cpp
namespace oxygen::engine::renderer::sceneprep {

// Collection-specific concepts using same pattern as finalization
template<typename F>
concept CollectionFilter = requires(F f, const RenderItemProto& item, const ScenePrepContext& ctx) {
  { f(item, ctx) } -> std::same_as<bool>; // Return true to continue processing
};

template<typename E>
concept CollectionExtractor = requires(E e, RenderItemProto& item, const ScenePrepContext& ctx) {
  { e(item, ctx) } -> std::same_as<void>; // Extract data into RenderItemProto
};

template<typename P>
concept CollectionProducer = requires(P p, const RenderItemProto& item, const ScenePrepContext& ctx,
                                      std::vector<RenderItemData>& output) {
  { p(item, ctx, output) } -> std::same_as<void>; // Emit RenderItemData
};

// Collection configuration with optional stages
template<typename PreFilter = void, typename TransformExtractor = void,
         typename MeshResolver = void, typename MaterialExtractor = void,
         typename GeometryExtractor = void, typename Producer = void>
struct CollectionConfig {
  // Optional stages - use void to indicate "not provided"
  [[no_unique_address]] PreFilter pre_filter{};
  [[no_unique_address]] TransformExtractor transform_extractor{};
  [[no_unique_address]] MeshResolver mesh_resolver{};
  [[no_unique_address]] MaterialExtractor material_extractor{};
  [[no_unique_address]] GeometryExtractor geometry_extractor{};
  [[no_unique_address]] Producer producer{};

  // Helper to check if stage is available
  static constexpr bool has_pre_filter = !std::is_void_v<PreFilter>;
  static constexpr bool has_transform_extractor = !std::is_void_v<TransformExtractor>;
  static constexpr bool has_mesh_resolver = !std::is_void_v<MeshResolver>;
  static constexpr bool has_material_extractor = !std::is_void_v<MaterialExtractor>;
  static constexpr bool has_geometry_extractor = !std::is_void_v<GeometryExtractor>;
  static constexpr bool has_producer = !std::is_void_v<Producer>;
};

// Finalization configuration with optional stages
template<typename Filter = void, typename TransformUploader = void,
         typename MaterialUploader = void, typename GeometryUploader = void,
         typename Sorter = void, typename TransformAssembler = void,
         typename MaterialAssembler = void, typename GeometryAssembler = void,
         typename FlagsAssembler = void, typename DrawMetadataMaker = void>
struct FinalizationConfig {
  // Optional stages - use void to indicate "not provided"
  [[no_unique_address]] Filter filter{};
  [[no_unique_address]] TransformUploader transform_uploader{};
  [[no_unique_address]] MaterialUploader material_uploader{};
  [[no_unique_address]] GeometryUploader geometry_uploader{};
  [[no_unique_address]] Sorter sorter{};
  [[no_unique_address]] TransformAssembler transform_assembler{};
  [[no_unique_address]] MaterialAssembler material_assembler{};
  [[no_unique_address]] GeometryAssembler geometry_assembler{};
  [[no_unique_address]] FlagsAssembler flags_assembler{};
  [[no_unique_address]] DrawMetadataMaker draw_metadata_maker{};

  // Helper to check if stage is available
  static constexpr bool has_filter = !std::is_void_v<Filter>;
  static constexpr bool has_transform_uploader = !std::is_void_v<TransformUploader>;
  static constexpr bool has_material_uploader = !std::is_void_v<MaterialUploader>;
  static constexpr bool has_geometry_uploader = !std::is_void_v<GeometryUploader>;
  static constexpr bool has_sorter = !std::is_void_v<Sorter>;
  static constexpr bool has_transform_assembler = !std::is_void_v<TransformAssembler>;
  static constexpr bool has_material_assembler = !std::is_void_v<MaterialAssembler>;
  static constexpr bool has_geometry_assembler = !std::is_void_v<GeometryAssembler>;
  static constexpr bool has_flags_assembler = !std::is_void_v<FlagsAssembler>;
  static constexpr bool has_draw_metadata_maker = !std::is_void_v<DrawMetadataMaker>;
};

// Factory functions for common configurations
auto CreateBasicCollectionConfig() {
  auto pre_filter = [](const RenderItemProto& item, const ScenePrepContext& ctx) -> bool {
    // Ensure node has both components and geometry available
    return item.Renderable().GetGeometry();
  };

  auto transform_extractor = [](RenderItemProto& item, const ScenePrepContext& ctx) -> void {
    // Populate proto fields from facades; exact fields live on the RenderItemProto
    // For example: item.proto.world_transform = item.Transform().GetWorldMatrix();
  };

  auto mesh_resolver = [](RenderItemProto& item, const ScenePrepContext& ctx) -> void {
    // LOD selection and mesh resolution logic using Renderable facade
    // e.g. item.ResolveMesh(item.Renderable().GetGeometry() ? item.Renderable().GetGeometry()->MeshAt(0) : nullptr, 0);
  };

  auto producer = [](const RenderItemProto& item, const ScenePrepContext& ctx,
                     std::vector<RenderItemData>& output) -> void {
    // Emit RenderItemData entries based on the resolved proto state (equivalent to EmitPerVisibleSubmesh)
    if (auto geom = item.Renderable().GetGeometry()) {
      // Example: iterate submeshes of resolved mesh if available
      // auto mesh = geom->MeshAt(item.ResolvedMeshIndex());
      // for (std::size_t submesh_idx = 0; submesh_idx < mesh->GetSubmeshCount(); ++submesh_idx) { ... }
      RenderItemData d;
      // fill d from item
      output.push_back(std::move(d));
    }
  };

  return CollectionConfig<decltype(pre_filter), decltype(transform_extractor),
                         decltype(mesh_resolver), void, void, decltype(producer)>{
    .pre_filter = std::move(pre_filter),
    .transform_extractor = std::move(transform_extractor),
    .mesh_resolver = std::move(mesh_resolver),
    .producer = std::move(producer)
  };
}

auto CreateBasicScenePrepConfig() {
  auto filter = [](const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item) -> PassMask {
    PassMask mask = 0;
    switch (item.domain) {
      case MaterialDomain::kOpaque: mask |= (1u << 0); break;
      case MaterialDomain::kTransparent: mask |= (1u << 1); break;
      case MaterialDomain::kCutout: mask |= (1u << 0); break;
    }
    if (item.cast_shadows) mask |= (1u << 2);
    return mask;
  };

  auto transform_uploader = [](std::span<const RenderItemData> all_items,
                               std::span<const std::size_t> indices,
                               const ScenePrepContext& ctx, ScenePrepState& state) -> void {
    for (auto idx : indices) {
      const auto& transform = all_items[idx].world_transform;
      auto handle = state.transform_mgr.GetOrAllocate(transform);
      state.transform_cache.MapItemToHandle(idx, handle);
    }
    state.transform_mgr.FlushPendingUploads();
  };

  auto geometry_assembler = [](const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item,
                               const ScenePrepState& state, RenderItem& out) -> void {
    if (item.geometry) {
      out.mesh = item.geometry->MeshAt(item.lod_index);
      out.submesh_index = item.submesh_index;
      auto geom_handle = state.geometry_cache.GetHandle(item.geometry.get());
      if (geom_handle) {
        out.vertex_buffer_handle = geom_handle->vertex_buffer;
        out.index_buffer_handle = geom_handle->index_buffer;
      }
    }
  };

  return FinalizationConfig<decltype(filter), decltype(transform_uploader), void, void, void,
                           void, void, decltype(geometry_assembler)>{
    .filter = std::move(filter),
    .transform_uploader = std::move(transform_uploader),
    .geometry_assembler = std::move(geometry_assembler)
  };
}

// Test-friendly configurations (GPU-independent)
  auto CreateMockCollectionConfig() {
  // For testing - no real work, just data flow validation
  auto mock_filter = [](const RenderItemProto& item, const ScenePrepContext& ctx) -> bool {
    return true; // Accept all items for testing
  };

  auto mock_producer = [](const RenderItemProto& item, const ScenePrepContext& ctx,
                          std::vector<RenderItemData>& output) -> void {
    auto& render_item = output.emplace_back();
    render_item.world_transform = glm::mat4(1.0f); // Identity for testing
    render_item.submesh_index = 0;
    render_item.lod_index = 0;
    // Minimal setup for testing
  };

  return CollectionConfig<decltype(mock_filter), void, void, void, void, decltype(mock_producer)>{
    .pre_filter = std::move(mock_filter),
    .producer = std::move(mock_producer)
  };
}

auto CreateMockScenePrepConfig() {
  // For testing - no GPU operations
  auto mock_filter = [](const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item) -> PassMask {
    return 1u; // Single pass for testing
  };

  auto mock_assembler = [](const ScenePrepContext& ctx, ScenePrepState& state, const RenderItemData& item,
                           const ScenePrepState& state, RenderItem& out) -> void {
    out.mesh = nullptr; // Mock mesh
    out.submesh_index = item.submesh_index;
    out.vertex_buffer_handle = 42; // Mock handle
    out.index_buffer_handle = 43;  // Mock handle
  };

  return FinalizationConfig<decltype(mock_filter), void, void, void, void, void, void,
                           decltype(mock_assembler)>{
    .filter = std::move(mock_filter),
    .geometry_assembler = std::move(mock_assembler)
  };
}
}
```

### Unified ScenePrep Pipeline

```cpp
namespace oxygen::engine::renderer::sceneprep {

template<typename Config>
class ScenePrepPipeline {
public:
  explicit ScenePrepPipeline(Config config) : config_(std::move(config)) {}

  //! Single-call ScenePrep: Collection + Finalization
  auto ScenePrep(scene::Scene& scene, const View& view, std::uint64_t frame_id,
                 RenderContext& render_context, ScenePrepState& state) -> ScenePrepResult {

    ScenePrepContext ctx{view, scene, frame_id, render_context};
    state.ResetFrameData();

    // Phase 1: Collection (using configurable pipeline)
    CollectItems(scene, ctx, state);

    // Phase 2: Finalization (using configurable finalizers)
    return FinalizeItems(ctx, state);
  }

  //! Split-phase access for advanced use cases
  auto CollectOnly(scene::Scene& scene, const View& view, std::uint64_t frame_id,
                   ScenePrepState& state) -> void {
    ScenePrepContext ctx{view, scene, frame_id, /*render_context=*/nullptr};
    state.ResetFrameData();
    CollectItems(scene, ctx, state);
  }

  auto FinalizeOnly(const View& view, std::uint64_t frame_id,
                    RenderContext& render_context, ScenePrepState& state) -> ScenePrepResult {
    ScenePrepContext ctx{view, scene, frame_id, render_context};
    return FinalizeItems(ctx, state);
  }

private:
  Config config_;

  auto CollectItems(scene::Scene& scene, const ScenePrepContext& ctx, ScenePrepState& state) -> void {
    const auto& node_table = scene.GetNodes();
    const auto span = node_table.Items();

    for (const auto& node_impl : span) {
      RenderItemProto wi{node_impl};

      // Apply configurable collection pipeline with graceful stage skipping
      if constexpr (Config::CollectionConfig::has_pre_filter) {
        if (!config_.collection.pre_filter(wi, ctx)) continue;
      }

      if constexpr (Config::CollectionConfig::has_transform_extractor) {
        config_.collection.transform_extractor(wi, ctx);
      }

      if constexpr (Config::CollectionConfig::has_mesh_resolver) {
        config_.collection.mesh_resolver(wi, ctx);
      }

      if constexpr (Config::CollectionConfig::has_material_extractor) {
        config_.collection.material_extractor(wi, ctx);
      }

      if constexpr (Config::CollectionConfig::has_geometry_extractor) {
        config_.collection.geometry_extractor(wi, ctx);
      }

      if constexpr (Config::CollectionConfig::has_producer) {
        config_.collection.producer(wi, ctx, state.collected_items);
      }
    }
  }

  auto FinalizeItems(const ScenePrepContext& ctx, ScenePrepState& state) -> ScenePrepResult {
    // Phase 1: Filter items (required for finalization)
    if constexpr (Config::FinalizationConfig::has_filter) {
      for (std::size_t i = 0; i < state.collected_items.size(); ++i) {
        auto mask = config_.finalization.filter(state.collected_items[i], ctx, state);
        if (mask != 0) {
          state.filtered_indices.push_back(i);
          state.pass_masks.push_back(mask);
        }
      }
    } else {
      // Default: accept all items with single pass
      state.filtered_indices.resize(state.collected_items.size());
      std::iota(state.filtered_indices.begin(), state.filtered_indices.end(), 0);
      state.pass_masks.assign(state.collected_items.size(), 1u);
    }

    // Phase 2: Batch uploads (optional)
    if constexpr (Config::FinalizationConfig::has_transform_uploader) {
      config_.finalization.transform_uploader(state.collected_items, state.filtered_indices, ctx, state);
    }

    if constexpr (Config::FinalizationConfig::has_material_uploader) {
      config_.finalization.material_uploader(state.collected_items, state.filtered_indices, ctx, state);
    }

    if constexpr (Config::FinalizationConfig::has_geometry_uploader) {
      config_.finalization.geometry_uploader(state.collected_items, state.filtered_indices, ctx, state);
    }

    // Phase 3: Sort and partition (optional)
    std::vector<std::size_t> order = state.filtered_indices; // Default: input order
    PartitionMap partitions;

    if constexpr (Config::FinalizationConfig::has_sorter) {
      auto [sorted_order, computed_partitions] = config_.finalization.sorter(
        state.collected_items, state.filtered_indices, ctx, state);
      order = std::move(sorted_order);
      partitions = std::move(computed_partitions);
    } else {
      // Default partitioning: single partition with all items
      partitions[0] = {0, order.size()};
    }

    // Phase 4: Assemble final render items
    RenderItemsList items;
    std::vector<DrawMetadata> draw_metadata;
    items.Reserve(order.size());

    for (std::size_t k = 0; k < order.size(); ++k) {
      const auto idx = order[k];
      const auto& input_item = state.collected_items[idx];

      RenderItem item{};
      item.pass_mask = state.pass_masks[k];

      // Apply assemblers with graceful skipping
      if constexpr (Config::FinalizationConfig::has_transform_assembler) {
        config_.finalization.transform_assembler(input_item, ctx, state, item);
      }

      if constexpr (Config::FinalizationConfig::has_material_assembler) {
        config_.finalization.material_assembler(input_item, ctx, state, item);
      }

      if constexpr (Config::FinalizationConfig::has_geometry_assembler) {
        config_.finalization.geometry_assembler(input_item, ctx, state, item);
      }

      if constexpr (Config::FinalizationConfig::has_flags_assembler) {
        config_.finalization.flags_assembler(input_item, ctx, state, item);
      }

      items.Add(std::move(item));

      // Generate draw metadata (optional)
      if constexpr (Config::FinalizationConfig::has_draw_metadata_maker) {
        if (item.mesh) {
          for (const auto& view : item.mesh->GetViews(item.submesh_index)) {
            auto metadata = config_.finalization.draw_metadata_maker(view, ctx, state);
            draw_metadata.push_back(metadata);
          }
        }
      } else {
        // Default draw metadata
        DrawMetadata metadata{};
        metadata.first_index = 0;
        metadata.index_count = item.mesh ? item.mesh->GetIndexCount(item.submesh_index) : 0;
        metadata.base_vertex = 0;
        draw_metadata.push_back(metadata);
      }
    }

    return ScenePrepResult{
      .items = std::move(items),
      .draw_metadata = std::move(draw_metadata),
      .partitions = std::move(partitions),
      .collected_count = state.collected_items.size(),
      .filtered_count = state.filtered_indices.size()
    };
  }
};

// Convenience type alias for typical usage
template<typename CollectionConfig, typename FinalizationConfig>
using ConfiguredScenePrepPipeline = ScenePrepPipeline<ScenePrepConfig<CollectionConfig, FinalizationConfig>>;
}
```

### Removed Legacy Builder

All references to the prior `RenderListBuilder` have been removed from code. No
further migration steps are required.

### Migration Strategy Status

Legacy migration is complete. Remaining migration work concerns *internal pass
consumers* (eliminating the SoA→AoS bridge) rather than Extraction removal.

### Benefits of Unified Design (Anticipated)

1. **Consistency**: Both phases use same concept-based configuration pattern
2. **Flexibility**: Collection pipeline becomes configurable like finalization
3. **Performance**: Eliminates vector copy between Collection and Finalization
4. **Maintainability**: Single state object, consistent error handling
5. **Testability**: Both phases can be tested with same mocking strategies
6. **Extensibility**: Easy to add new extractors/finalizers using same patterns

This refactoring creates a cohesive extraction module that maintains backward compatibility while providing a much cleaner, more flexible architecture for future development.

### Input Requirements (Planned Finalization)

The finalization system expects:

```cpp
// Input from RenderListBuilder::Collect()
std::vector<RenderItemData> collected_items; // Pre-culled, with resolved LODs

// Required context
FinalizeContext ctx {
  .view = current_view,           // Camera matrices, frustum (for sorting)
  .frame_id = current_frame_id,   // For temporal tracking
  .render_context = gpu_context   // GPU device and command lists
};

// State that persists across frames
FinalizeState state {
  .transform_mgr = persistent_transform_manager,
  .material_registry = persistent_material_registry,
  .geometry_registry = persistent_geometry_registry,
  // ... per-frame caches are reset each frame
};
```

### Output Integration (Planned)

```cpp
// Outputs ready for renderer
struct FinalizeOutput {
  RenderItemsList items;              // GPU-ready render items
  std::vector<DrawMetadata> draw_metadata; // One per mesh-view draw
  PartitionMap partitions;            // Pass-specific ranges
};

// Usage in renderer
void Renderer::SubmitFrame(const FinalizeOutput& output) {
  for (auto pass_id : enabled_passes) {
    auto range = output.partitions.at(pass_id);
    for (std::size_t i = range.first; i < range.second; ++i) {
      const auto& item = output.items[i];
      const auto& draw_meta = output.draw_metadata[i];

      // Bind pipeline state based on item.material, item.domain
      // Issue draw call using draw_meta ranges and item handles
      DrawIndexed(draw_meta.index_count, draw_meta.first_index, draw_meta.base_vertex);
    }
  }
}
```

### Error Handling Details (Planned)

Common failure modes and recovery strategies:

```cpp
// Handle resource exhaustion gracefully
if (transform_upload_fails) {
  // Fallback: Use previous frame's transform or identity
  item.transform_handle = GetFallbackTransformHandle();
  LogWarning("Transform upload failed, using fallback");
}

if (material_missing) {
  // Fallback: Use default material
  item.material = MaterialAsset::GetDefault();
  item.material_handle = GetDefaultMaterialHandle();
}

if (geometry_not_resident) {
  // Skip this frame, schedule for next frame
  continue; // Don't add to output
  ScheduleGeometryUpload(item.geometry);
}

// Validate outputs before returning
auto ValidateOutput(const FinalizeOutput& output) -> bool {
  if (output.items.size() != output.draw_metadata.size()) return false;

  for (const auto& item : output.items) {
    if (!item.IsValid()) return false; // Check all handles are valid
  }

  return true;
}
```

### Thread Safety (Planned Emphasis)

- **FinalizeState**: Not thread-safe, use one instance per rendering thread
- **Persistent Registries**: Must be thread-safe for multi-view rendering
- **GPU Resources**: Synchronize uploads with rendering using fences/barriers

```cpp
// Example multi-view setup
void RenderMultipleViews(std::span<const View> views) {
  std::vector<std::future<FinalizeOutput>> futures;

  for (const auto& view : views) {
    futures.emplace_back(std::async(std::launch::async, [&view]() {
      FinalizeState local_state = CreatePerThreadState();
      return Finalize(collected_items_for_view, view, local_state, config);
    }));
  }

  // Collect results and submit
  for (auto& future : futures) {
    auto output = future.get();
    SubmitToGPU(output);
  }
}
```

# Render Items Extraction Design

This document describes a improved render item extraction system for the Oxygen
Engine. The design introduces a two-phase approach (Collect + Finalize) to
efficiently extract renderable objects from the scene graph and prepare them for
GPU submission.

---

## Implementation Tasks Summary

| Component/Task     | Status         | Notes                                              |
| ------------------ | -------------- | -------------------------------------------------- |
| Design             | ✅ Complete    | Architecture and contracts                         |
| Types              | ❌ Not Started | Add RenderItemData header + docs                   |
| Builder            | ❌ Not Started | Collect/Finalize/Evict API + PIMPL                 |
| Extractors         | ❌ Not Started | LOD, visibility, material, bounds (pure)           |
| Finalizers API     | ❌ Not Started | Transform, material, geometry, draw interfaces     |
| Finalizers         | ❌ Not Started | Default impls + No-GPU variants                    |
| Transforms         | ❌ Not Started | Dirty detect, slots, LRU, growth                   |
| Uploads            | ❌ Not Started | Coalescing, double/triple buffer, 3x4 layout       |
| Finalize           | ❌ Not Started | Finalizer-driven, CPU-only deterministic           |
| Sorting            | ❌ Not Started | Domain buckets + batching keys                     |
| Residency          | ❌ Not Started | Touch stamps, keep window, eviction                |
| Metrics            | ❌ Not Started | Counts, bytes, ranges, hits/misses                 |
| Multi-view         | ❌ Not Started | Per-view runs, cache reuse                         |
| Errors             | ❌ Not Started | Fallbacks + rate-limited logs                      |
| Tests: Collect     | ❌ Not Started | LOD/visibility/material cases                      |
| Tests: Finalize    | ❌ Not Started | No-GPU path, sorting assertions                    |
| Tests: Transforms  | ❌ Not Started | Dirty/coalescing/threshold paths                   |
| Integration        | ❌ Not Started | Wire passes + A/B flag                             |
| Docs               | ❌ Not Started | README + examples + config                         |

---

## Table of Contents

- [Introduction](#introduction)
- [Key Concepts](#key-concepts)
- [Architecture Overview](#architecture-overview)
- [Data Types](#data-types)
- [API Design](#api-design)
- [Implementation Guide](#implementation-guide)
- [Integration Points](#integration-points)
- [Migration Plan](#migration-plan)
- [Reference](#reference)

---

## Introduction

The render items extraction system is responsible for transforming scene data
into GPU-ready render commands. This involves traversing the scene graph,
applying LOD selection and culling, resolving materials and geometry, and
organizing the results for efficient GPU submission.

### Why Two-Phase Extraction?

Current extraction (in `SceneExtraction.cpp`) performs all work during scene
traversal, mixing scene queries with GPU resource management. This creates
several problems:

1. **Performance**: GPU resource operations during traversal create cache misses
   and stalls
2. **Complexity**: LOD hysteresis and residency tracking scattered throughout
   traversal code
3. **Maintainability**: Difficult to extend with new features like multi-pass
   rendering or temporal filtering

The two-phase approach separates these concerns:

- **Collect Phase**: Fast scene traversal producing lightweight references
- **Finalize Phase**: Off-scene GPU resource resolution, sorting, and
  optimization

### Current State vs Target

| Aspect | Current (`SceneExtraction.cpp`) | Target (This Design) |
|--------|-------------------------------|---------------------|
| Granularity | One `RenderItem` per mesh | Configurable: mesh or submesh level |
| GPU Work | Mixed with scene traversal | Isolated to Finalize phase |
| Caching | Ad-hoc per-frame | Stateful builder with cross-frame caches |
| Extensibility | Monolithic function | Pluggable strategies |

---

## Key Concepts

### Core Types from Existing Modules

Understanding these existing types is essential before implementing the
extraction system:

#### From `oxygen::data`

- **`GeometryAsset`**: Container for all LOD levels of a mesh
- **`Mesh`**: Single LOD level containing vertex/index data and submeshes
- **`SubMesh`**: Material group within a mesh, containing one or more MeshViews
- **`MeshView`**: Non-owning slice of vertex/index data (actual draw call
  granularity)
- **`MaterialAsset`**: Shader and texture parameters for rendering

#### From `oxygen::scene`

- **`SceneNode`**: Scene graph node with optional renderable component
- **`RenderableComponent`**: Per-node rendering state (LOD policies, visibility,
  material overrides)
- **`ActiveMesh`**: Result of LOD selection for a node

#### From `oxygen::engine`

- **`RenderItem`**: Immutable GPU-ready snapshot for a single draw call
- **`RenderItemsList`**: Container for a frame's render items
- **`View`**: Camera and frustum information for culling

### Phase Responsibilities

#### Collect Phase

- Scene graph traversal using existing `SceneTraversal` or direct node iteration
- LOD selection via `RenderableComponent::SelectActiveMesh()`
- Visibility testing and material resolution
- Output: Lightweight `RenderItemData` records with asset references

#### Finalize Phase

- GPU resource resolution (vertex buffers, textures, etc.)
- Transform matrix uploads to bindless buffer
- Material constants preparation and upload
- Sorting and partitioning for efficient rendering
- Output: Populated `RenderItemsList`

---

## Architecture Overview

The extraction system is built around a stateful `RenderItemsListBuilder` that
maintains caches across frames:

```text
Scene Graph → [Collect] → RenderItemData[] → [Finalize] → RenderItemsList
     ↑                                                         ↓
     └─── Builder Caches (LOD hysteresis, residency) ←────────┘
```

### Data Flow

1. **Input**: `Scene&`, `View&` (camera + frustum), `frame_id`
2. **Collect**:
   - For each visible node with renderable component:
     - Apply LOD selection based on distance/screen size
     - Check submesh visibility flags
     - Resolve material (override → submesh default → engine default)
     - Emit `RenderItemData` with asset references
3. **Finalize**:
   - Resolve GPU handles for geometry, materials, transforms
   - Upload transforms to bindless buffer
   - Ensure geometry/texture residency
   - Sort items by material/geometry for batching
   - Populate `RenderItemsList`

Note: The builder runs per view. In multi-view scenarios (e.g., primary camera
plus shadow-caster views), invoke Collect/Finalize once per view; caches (LOD
hysteresis, residency) are reused across views for efficiency.

### Key Design Principles

- **Immutable Assets**: Scene assets never change during extraction
- **Lazy GPU Work**: No GPU operations during scene traversal
- **Cached State**: Builder maintains LOD hysteresis and residency across frames
- **Pluggable Finalizers**: Finalizers customize
  transform/material/geometry/draw handling during Finalize; Collect uses
  extractors only
- **Composable Extractors**: Collect is factored into small, pure extractors
  (LOD, visibility, material, bounds) for clarity and testability

---

## Data Types

### RenderItemData (Collect Output)

Lightweight record produced during scene traversal:

```cpp
//! Lightweight render item data collected during scene traversal.
/*!
 Contains minimal references to scene and asset data. No GPU resources
 or expensive computations are stored here - only what's needed to make
 rendering decisions during the Finalize phase.
*/
struct RenderItemData {
    // Scene identity
    scene::NodeHandle node_handle;
    std::uint32_t lod_index;
    std::uint32_t submesh_index;

    // Asset references (immutable, shareable)
    std::shared_ptr<const data::GeometryAsset> geometry;
    std::shared_ptr<const data::MaterialAsset> material;

    // Cached scene state
    data::MaterialDomain domain;  // opaque, masked, transparent
    glm::vec4 world_bounding_sphere;

    // Rendering flags
    bool cast_shadows = true;
    bool receive_shadows = true;
    std::uint32_t render_layer = 0;

    // Optional future extension for temporal tracking
    // std::uint64_t instance_id = 0;
};
```

**Design Notes**:

- Small, value-type friendly for efficient copying and vectorization
- References immutable assets via `shared_ptr` - no data duplication
- Transform not stored here - resolved during Finalize via node handle
- Optional `instance_id` for advanced temporal tracking can be added later
- Default identity policy: do not allocate persistent per-item IDs. Identity is
  `(node_handle, lod_index, submesh_index[, view])`. The optional `instance_id`
  is off by default and enabled only for temporal techniques.

### Updated RenderItem Usage

The existing `oxygen::engine::RenderItem` serves as the final GPU-ready
snapshot:

```cpp
// From existing RenderItem.h - no changes needed
struct RenderItem {
    std::shared_ptr<const data::Mesh> mesh;
    std::shared_ptr<const data::MaterialAsset> material;
    glm::mat4 world_transform;
    glm::mat4 normal_transform;
    // ... existing fields
};
```

During Finalize, `RenderItemData` records are converted to `RenderItem`
instances with:

- Resolved mesh from `geometry` asset using `lod_index`
- Current world transform from scene node
- Computed normal transform and bounding volumes

---

## API Design

### Main Builder API

```cpp
namespace oxygen::engine::extraction {

//! Stateful builder for extracting render items from scene data.
/*!
 Maintains cross-frame caches for LOD hysteresis, resource residency tracking,
 and transform management. Designed to live for the lifetime of a camera/view
 and be reused across multiple frames.

 The extraction process is split into two phases:
 1. Collect: Fast scene traversal producing lightweight references
 2. Finalize: GPU resource resolution, sorting, and optimization
*/
class RenderItemsListBuilder {
public:
    //! Construct builder with default configuration
    RenderItemsListBuilder();

    //! Phase 1: Collect render items from scene (CPU-only, no GPU work)
    [[nodiscard]] auto Collect(
        const scene::Scene& scene,
        const View& view,
        std::uint64_t frame_id
    ) -> std::vector<RenderItemData>;

    //! Phase 2: Finalize items into GPU-ready renderable list
    auto Finalize(
        std::span<const RenderItemData> collected_items,
        RenderContext& render_context,
        RenderItemsList& output
    ) -> void;

    //! Clean up stale resources that haven't been used recently
    auto EvictStaleResources(
        RenderContext& render_context,
        std::uint64_t current_frame_id,
        std::uint32_t keep_frame_count = 3
    ) -> void;

private:
    // Internal state and caches
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine::extraction
```

### Finalizers (extensibility at Finalize)

The builder uses pluggable Finalizers that run only during Finalize:

- Transform finalizer: allocation policy for transform handles/slots and upload
  scheduling
- Material finalizer: material constants/textures packaging and residency
- Geometry finalizer: geometry handle resolution and residency
- Draw finalizer: batching keys and final item assembly

Collect remains extractor-only and GPU-free. Default finalizers are provided to
match the behavior described in this document.

### CollectContext vs GPU SceneConstants

- CollectContext (implicit in the `View` and per-frame inputs) is CPU-only data
  used to make decisions during Collect (e.g., frustum, jitter, view id). It is
  not stored in items.
- GPU SceneConstants are built/updated during Finalize per view (and per pass if
  needed) by the renderer/finalizers. They are uploaded once and referenced via
  bindless descriptors; they are not duplicated per item.

### Usage Example

```cpp
// Setup (once per camera/view)
auto builder = extraction::RenderItemsListBuilder{};

// Per frame
auto collected = builder.Collect(scene, camera_view, frame_id);
builder.Finalize(collected, render_context, render_items_list);

// Render using existing pipeline
for (const auto& item : render_items_list.Items()) {
    // Submit draw calls as normal
}

// Cleanup (optional, can be done less frequently)
builder.EvictStaleResources(render_context, frame_id);
```

---

## Implementation Guide

### Pipeline configuration (pseudo)

```text
cfg.extractors = {
    lod:        LodResolver(),
    visibility: VisibilityFilter(),
    material:   MaterialResolver(fallback = DefaultMaterial),
    bounds:     BoundsResolver(enableSubmeshAabb = false)
}

cfg.finalizers = {
    transform: PersistentSlotsStrategy() | CompactPerFrameStrategy(),
    material:  DefaultMaterialStrategy(),
    geometry:  DefaultGeometryStrategy(),
    draw:      DefaultDrawStrategy()
}
```

### Collect pipeline (pseudo)

```text
function Collect(scene, view, cfg) -> list<RenderItemData>
    items = []
    participating_nodes = set()

    for each (handle, node_impl) in scene.nodes:
        if not node_impl.hasRenderable: continue

        renderable = SceneNode(handle, scene).Renderable()

        // 1) LOD
        if not cfg.extractors.lod.resolve(renderable, view): continue
        active = renderable.activeMesh()
        if active is null: continue

        // 2) Bounds
        worldSphere = cfg.extractors.bounds.worldSphere(renderable)

        // 3) Visibility + material per submesh
        mesh = active.mesh
        for si in 0..mesh.submeshCount-1:
            if not cfg.extractors.visibility.isVisible(renderable, active.lodIndex, si): continue
            material = cfg.extractors.material.resolve(renderable, active.lodIndex, si)
            if material is null: material = cfg.extractors.material.fallback()

            items.append(RenderItemData{
                node_handle: handle,
                lod_index: active.lodIndex,
                submesh_index: si,
                geometry: active.geometryAsset,
                material: material,
                domain: material.domain(),
                world_bounding_sphere: worldSphere,
                cast_shadows: renderable.castsShadows(),
                receive_shadows: renderable.receivesShadows(),
                render_layer: renderable.renderLayer()
            })

            participating_nodes.add(handle)

    return items, participating_nodes
```

### Finalization

The Finalize phase converts collected data into GPU-ready render items:

```text
function Finalize(scene, view, frame_id, collected_items, participating_nodes, cfg, ctx, output)
    // 1) Transforms (strategy): returns mapping collect-identity -> gpuIndex (or stable index in No-GPU mode)
    transform_map = cfg.finalizers.transform.finalize(scene, participating_nodes, ctx, frame_id)

    // 2) Materials/Geometry (strategies): ensure residency/build handles; no-ops in No-GPU mode
    cfg.finalizers.material.prepare(collected_items, ctx, frame_id)
    cfg.finalizers.geometry.prepare(collected_items, ctx, frame_id)

    // 3) Build final items (CPU only)
    final_items = []
    for each d in collected_items:
        world = scene.worldTransform(d.node_handle)
        normal = computeNormalTransform(world)
        mesh = d.geometry.meshFor(d.lod_index)

        matHandle = cfg.finalizers.material.handle(d.material)
        geoHandle = cfg.finalizers.geometry.handle(d.geometry, d.lod_index, d.submesh_index)
        xformIdx  = cfg.finalizers.transform.index(d.node_handle, transform_map)

        item = cfg.finalizers.draw.assemble(d, mesh, matHandle, geoHandle, xformIdx, world, normal, ctx)
        final_items.append(item)

    // 4) Sort/partition for efficient submission
    cfg.finalizers.draw.sortAndPartition(final_items)

    // 5) Populate output snapshot
    output.replaceWith(final_items)
```

### Transform Management

Transform handling requires efficient dirty detection and GPU upload strategies:

```cpp
class TransformManager {
public:
    //! Upload dirty transforms for the current frame using scene dirty list
    auto UploadDirtyTransforms(
        const scene::Scene& scene,
        std::span<const scene::NodeHandle> participating_nodes,
        RenderContext& context,
        std::uint64_t frame_id
    ) -> void;

    //! Get GPU buffer index for a node's transform
    [[nodiscard]] auto GetTransformIndex(scene::NodeHandle node) const -> std::uint32_t;

    //! Evict unused transform slots (called periodically)
    auto EvictUnusedSlots(std::uint64_t current_frame_id, std::uint32_t keep_frames) -> void;

private:
    struct TransformSlot {
        scene::NodeHandle node_handle;
        std::uint32_t gpu_index;
        std::uint64_t last_used_frame;
        glm::mat4 cached_transform;  // For change detection if no scene dirty list
    };

    // Transform slot management
    std::unordered_map<scene::NodeHandle, std::uint32_t> node_to_slot_;
    std::vector<TransformSlot> transform_slots_;
    std::vector<std::uint32_t> free_slots_;

    // GPU buffer management
    std::uint32_t buffer_capacity_ = 0;
    std::uint32_t next_slot_id_ = 0;
};
```

#### Dirty Detection Strategy

The implementation supports multiple dirty detection approaches:

##### Option A (Preferred): Scene-Provided Dirty List

```cpp
// Scene provides list of nodes with dirty transforms
auto dirty_nodes = scene.GetDirtyTransformNodes();
UploadDirtyTransforms(scene, dirty_nodes, context, frame_id);
```

##### Option B (Fallback): Transform Hash Comparison

```cpp
// Compare cached transform hash against current
auto current_transform = scene.GetWorldTransform(node_handle);
if (Hash(current_transform) != cached_hash) {
    // Transform changed, add to upload list
}
```

#### GPU Upload Strategy

```cpp
auto TransformManager::UploadDirtyTransforms(
    const scene::Scene& scene,
    std::span<const scene::NodeHandle> participating_nodes,
    RenderContext& context,
    std::uint64_t frame_id
) -> void {

    // Step 1: Collect dirty transforms and assign/reuse GPU indices
    auto upload_records = std::vector<UploadRecord>{};

    for (auto node_handle : participating_nodes) {
        auto gpu_index = GetOrAssignSlot(node_handle, frame_id);
        auto world_transform = scene.GetWorldTransform(node_handle);

        // Check if transform actually changed (if no scene dirty list)
        if (HasTransformChanged(node_handle, world_transform)) {
            upload_records.emplace_back(UploadRecord{
                .gpu_index = gpu_index,
                .transform = world_transform
            });

            // Cache for next frame comparison
            transform_slots_[gpu_index].cached_transform = world_transform;
            transform_slots_[gpu_index].last_used_frame = frame_id;
        }
    }

    // Step 2: Sort by GPU index for range coalescing
    std::sort(upload_records.begin(), upload_records.end(),
        [](const auto& a, const auto& b) { return a.gpu_index < b.gpu_index; });

    // Step 3: Coalesce consecutive indices into ranges
    auto upload_ranges = CoalesceUploadRanges(upload_records);

    // Step 4: Upload each range efficiently
    for (const auto& range : upload_ranges) {
        context.UploadTransformRange(range.start_index, range.transforms);
    }
}
```

#### Buffer layout and lifetime

- Layout: prefer 3x4 row-major matrices with 64-byte stride for alignment and
  simple address arithmetic; optionally use full 4x4 if backend parity is
  required.
- Lifetime: double or triple buffer the device-local transform buffer per frame
  to avoid CPU/GPU hazards; keep the bindless SRV/descriptor stable while
  rotating buffers.

#### Key Design Decisions

**Persistent GPU Indices**: Transform slots persist across frames to avoid
rebinding

- Enables temporal effects (motion vectors, TAA)
- Reduces GPU descriptor updates
- Slots evicted based on usage (LRU policy)

**Range Coalescing**: Sort dirty transforms by GPU index and upload consecutive
ranges

- Minimizes number of GPU copy operations
- Typical case: few large uploads instead of many small ones
- Fall back to full buffer refresh if dirty coverage > threshold (e.g., 50%)

**Memory Management**:

- Pre-allocate transform buffer with reasonable initial size
- Grow geometrically when needed (1.5x-2x growth factor)
- Compact/defragment during low-activity periods

**Alternate mode (deferred)**: Instead of persistent slots, a per-frame compact
mapping from collect indices to GPU indices can be used to tightly pack
transforms for the view. This trades off cross-frame stability for tighter
compaction and reduced fragmentation. Keep this as an option if profiling shows
benefits.

This approach achieves O(D log D) complexity for D dirty transforms while
minimizing GPU upload bandwidth.

---

## Error Handling and Edge Cases

### Error Recovery Strategy

The extraction system must be robust and provide clear diagnostics:

**Missing Assets**:

- Missing geometry → Skip item, log once per asset (rate-limited)
- Missing material → Use engine default material
  (`MaterialAsset::CreateDefault()`)
- Missing LOD → Skip node, continue with other nodes

**Malformed Data**:

- Empty geometry/zero LODs → Skip during Collect phase
- Invalid submesh indices → Clamp to valid range, log warning
- Vertex-only meshes → Backend issues non-indexed draws (no special handling
  needed)

**Resource Failures**:

- GPU allocation failures → Graceful degradation with reduced quality
- Upload failures → Retry with smaller batches, fallback to per-item uploads

### Common Edge Cases

**Mesh Variations**:

- **Vertex-only meshes**: `Mesh::IndexBuffer().Empty()` → Backend issues
  non-indexed draw
- **Mixed index types**: 16/32-bit indices stored in `IndexBufferView`, backend
  handles conversion
- **Empty submeshes**: Skipped during visibility filtering

**Transform Edge Cases**:

- **Non-uniform scale**: World bounding sphere uses max-axis scale
- **Invalid transforms**: Use identity matrix, log warning
- **Destroyed nodes**: Handle via `NodeHandle` generation checks

**Material Edge Cases**:

- **Domain mismatch**: Use material domain from asset, override flags if needed
- **Shader compilation failure**: Fall back to unlit/debug shader
- **Texture loading failure**: Use default textures (checkerboard, white, normal
  map)

### Debugging Support

**Extraction Statistics (core, always-on in debug)**:

- Items collected/rejected per phase
- GPU upload bandwidth and timing
- Resource residency hit/miss rates

**Visualization Aids**:

- Color-code items by extraction phase or error state
- Overlay LOD selection decisions
- Show transform cache utilization

All logs related to missing assets or skips must be rate-limited. Counters are
stamped with `frame_id` and exposed for A/B comparisons between old and new
extraction paths.

---

## Integration Points

### With Existing Renderer

The new extraction system integrates cleanly with existing render passes:

```cpp
// In DepthPrePass::Execute() or ShaderPass::Execute()
auto builder = GetRenderItemsBuilder(); // Get from renderer
auto collected = builder.Collect(scene, view, frame_id);
builder.Finalize(collected, render_context, render_items_list);

// Use existing render loop
for (const auto& item : render_items_list.Items()) {
    // Same submission code as before
    SubmitDrawCall(item, render_context);
}
```

### With Scene System

The system respects all existing scene contracts:

- Uses `SceneNode::Renderable()` facade for all scene queries
- Respects LOD policies in `RenderablePolicies.h`
- Works with existing scene traversal patterns
- No modification to scene data structures required

### With Data System

Leverages existing immutable asset design:

- `GeometryAsset` and `MaterialAsset` used directly via `shared_ptr`
- No duplication of mesh or material data
- Respects asset versioning and lifecycle

---

## Migration Plan

### Phase 1: Basic Implementation

**Goal**: Replace `SceneExtraction.cpp` with equivalent functionality

- Implement `RenderItemsListBuilder` with mesh-level granularity
- Create `RenderItemData` type
- Implement basic `TransformManager` with dirty detection and range coalescing
- Basic Collect/Finalize with essential GPU resource management
- **Success Criteria**: Same visual output as current system with acceptable
  performance

### Phase 2: Performance Optimization

**Goal**: Optimize and harden the extraction system

- Implement transform slot eviction and memory management
- Add LOD hysteresis to reduce flickering
- Optimize material and geometry residency tracking
- Add comprehensive error handling and edge case coverage
- **Success Criteria**: Better performance than baseline, especially for static
  scenes

### Phase 3: Advanced Features

**Goal**: Enable submesh-level rendering and multi-pass support

- Support submesh-level render items (when backend ready)
- Add pass mask system for multi-pass rendering
- Implement temporal filtering capabilities
- Add strategy pattern for extensibility
- **Success Criteria**: Support for deferred shading and advanced effects

### Migration Strategy

1. **Parallel Implementation**: Build new system alongside existing extraction
2. **A/B Testing**: Runtime flag to switch between old and new systems
3. **Gradual Rollout**: Enable new system for specific render passes first
4. **Validation**: Comprehensive testing with existing content

### Acceptance Criteria

- 1:1 mapping of `RenderItem`s to visible `MeshView`s produced by Collect.
- Residency consistency: no use-after-evict; evictions occur only outside the
  keep window.
- Transform uploads: dirty transforms are uploaded at most once per frame;
  coalesced ranges preferred over many small copies.

## Future Enhancements

The following features from the original design analysis may be added in future
iterations:

### Multi-Pass Rendering Support

**Goal**: Enable efficient multi-pass rendering (deferred shading, shadow
passes, etc.)

**Design**: Add pass mask bitset to `RenderItemData` to encode which passes each
item participates in:

```cpp
struct RenderItemData {
    // ... existing fields
    std::uint8_t pass_mask = 0xFF;  // Bitset: depth|forward|shadow|etc
};
```

**Principles**:

- Populate mask during Collect based on material domain and flags
- Single traversal feeds multiple passes
- Enables early rejection of items not relevant to current pass

### Temporal Instance Tracking

**Goal**: Support temporal effects requiring cross-frame correlation

**Design**: Optional instance ID system for tracking render items across frames:

```cpp
struct RenderItemData {
    // ... existing fields
    std::uint64_t instance_id = 0;  // Optional: (node_id<<24)|(lod<<16)|(submesh<<8)|view
};
```

**Principles**:

- Opt-in feature for advanced rendering techniques
- Stable IDs while scene entities exist
- Enables temporal anti-aliasing, motion blur, occlusion culling history

---

## Reference

### Key Files to Study

**Existing Codebase**:

- `src/Oxygen/Renderer/Extraction/SceneExtraction.cpp` - Current baseline
- `src/Oxygen/Renderer/RenderItem.h` - Final render item format
- `src/Oxygen/Scene/SceneNode.h` - Scene graph node API
- `src/Oxygen/Data/GeometryAsset.h` - Asset structure

**Related Documentation**:

- `src/Oxygen/Renderer/README.md` - Renderer architecture overview
- `src/Oxygen/Data/README.md` - Data module guide
- Design documents in `design/` folder

Conceptual parallels: Unreal Engine’s two-stage flow (gather primitives, then
build mesh draw commands) mirrors the Collect/Finalize split here.

### Glossary

- **LOD**: Level of Detail - different mesh resolutions for distance-based
  optimization
- **Submesh**: Material group within a mesh sharing the same shader/textures
- **MeshView**: Slice of vertex/index data representing actual GPU draw call
- **Residency**: Whether a resource is currently loaded in GPU memory
- **Bindless**: GPU resource access via global indices rather than per-draw
  bindings

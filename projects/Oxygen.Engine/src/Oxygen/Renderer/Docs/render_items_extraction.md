# Render Items Extraction Design

This document describes a improved render item extraction system for the Oxygen
Engine. The design introduces a two-phase approach (Collect + Finalize) to
efficiently extract renderable objects from the scene graph and prepare them for
GPU submission.

---

## Implementation Tasks Summary

| Component/Task     | Status  | Notes                                              |
| ------------------ | ------- | -------------------------------------------------- |
| Design             | ✅      | Architecture and contracts                         |
| Types              | ✅      | `RenderItemData.h` added (internal); DrawMetadata extended (first_index/base_vertex) |
| Builder            | ✅      | Collect/Finalize/Evict implemented; Finalize is monolithic (no pluggable finalizers yet) |
| Extractors         | ✅      | ShouldRenderPreFilter, Transform, NodeFlags, MeshResolver(LOD), Visibility, Material(no-op), EmitPerVisibleSubmesh |
| Finalizers API     | ❌      | Transform, material, geometry, draw interfaces     |
| Finalizers         | ❌      | Default impls + No-GPU variants                    |
| Transforms         | ❌      | Dirty detect, slots, LRU, growth                   |
| Uploads            | ❌      | Coalescing, double/triple buffer, 3x4 layout       |
| Finalize           | ✅      | CPU-only deterministic finalize inside Builder; finalizers API TBD |
| Sorting            | ❌      | Domain buckets + batching keys                     |
| Residency          | ❌      | Touch stamps, keep window, eviction                |
| Metrics            | ❌      | Counts, bytes, ranges, hits/misses                 |
| Multi-view         | ❌      | Per-view runs, cache reuse                         |
| Errors             | ❌      | Fallbacks + rate-limited logs                      |
| Tests: Collect     | ✅      | Smoke + LOD distance-policy per-view               |
| Tests: Finalize    | ❌      | No-GPU path, sorting assertions                    |
| Tests: Transforms  | ❌      | Dirty/coalescing/threshold paths                   |
| Integration        | ✅      | Renderer uses Builder; RenderPass per-submesh views; shaders match metadata |
| Docs               | ✅      | This doc updated; keep future phases intact         |

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

### Current implementation path (as of August 2025)

This subsection reflects the code that exists today. The older, more ambitious
plan (finalizers API, transform manager, configurable strategies) is kept below
under Deferred design for future phases.

#### Collect: extractor pipeline

Files: `Extraction/Extractors.h`, `Extraction/Extractors_impl.h`,
`Extraction/Pipeline.h`, `Extraction/RenderListBuilder.cpp`

Pipeline composition used today:

```text
ShouldRenderPreFilter -> TransformExtractor -> MeshResolver
-> VisibilityFilter -> NodeFlagsExtractor -> MaterialResolver -> EmitPerVisibleSubmesh
```

Sketch:

```text
function Collect(scene, view, frame_id) -> vector<RenderItemData>
  out = []
  pipeline = Pipeline(
    ShouldRenderPreFilter,
    TransformExtractor,
    MeshResolver,
    VisibilityFilter,
    NodeFlagsExtractor,
    MaterialResolver,
    EmitPerVisibleSubmesh)

  // Iterate dense node table for locality
  for node_impl in scene.GetNodes().Items():
    wi  = WorkItem(node_impl)
    ctx = { view, scene, frame_id }
    pipeline(wi, ctx, out)

  return out
```

Notes:

- `RenderItemData` includes `lod_index`, `submesh_index`, asset refs,
  `world_transform`, flags, and `world_bounding_sphere`.
- No `NodeHandle` is stored; transforms are cached into `RenderItemData` during
  Collect.

#### Finalize: monolithic conversion

Files: `Extraction/RenderListBuilder.cpp`, `RenderItem.h`

Sketch:

```text
function Finalize(collected, render_context, output)
  output.Clear()
  output.Reserve(collected.size())
  for d in collected:
    item = RenderItem{}
    item.mesh = d.geometry ? d.geometry->MeshAt(d.lod_index) : nullptr
    item.material = d.material
    item.submesh_index = d.submesh_index
    item.world_transform = d.world_transform
    item.UpdatedTransformedProperties()
    item.cast_shadows = d.cast_shadows
    item.receive_shadows = d.receive_shadows
    item.render_layer = d.render_layer
    output.Add(item)
```

Notes:

- Finalizers API is not yet introduced; this is a single, CPU-only conversion
  step.
- GPU resource ensuring/upload is handled by the renderer outside the builder.

#### Renderer and draw path alignment

Files: `Renderer.cpp`, `RenderPass.cpp`, `Types/DrawMetadata.h`,
`Graphics/Direct3D12/Shaders/*.hlsl`

- `Renderer::EnsureResourcesForDrawList`:
  - Iterates each `RenderItem`'s selected submesh and its `MeshViews()` and
    appends one `DrawMetadata` per view.
  - Populates `first_index` and `base_vertex` in `DrawMetadata` so shaders can
    fetch indices/vertices via bindless.
  - Duplicates world transform and material constants per view (simple and
    correct for now).
- `RenderPass::IssueDrawCalls`:
  - Iterates `MeshViews()` per item and issues one `Draw` per view.
  - Increments the draw index root constant per issued draw to match metadata
    order.
- Shaders (`DepthPrePass.hlsl`, `FullScreenTriangle.hlsl`) read
  `DrawMetadata.first_index`/`base_vertex` and compute the actual vertex index
  through the bindless index buffer.

---

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

### Per-submesh draw execution: Option A vs Option B

This project evaluated two approaches to achieve true per-submesh draw calls
while keeping the new two-phase Collect/Finalize pipeline.

- Option A (Chosen): Extend RenderItem with submesh_index
  - What: Add a single field to the final RenderItem snapshot carrying the
    selected submesh index. The pass-level draw loop uses this to issue draws
    per MeshView of that submesh.
  - How: Collect already emits per-submesh records. Finalize copies
    d.submesh_index into RenderItem. RenderPass iterates SubMesh.MeshViews() and
    issues one draw per view using the exact index/vertex ranges. No additional
    GPU-side structures are introduced.
  - Pros: Minimal changes, immediate correctness, works with existing bindless
    DrawMetadata (no extra fields). Keeps CPU emission simple.
  - Cons: Multiple draw calls per RenderItem (one per MeshView). Material
    constants may be duplicated per view in the current path.

- Option B: Introduce a dedicated draw-command builder
  - What: Keep RenderItem mesh-level, then build a separate list of low-level
    draw commands (one per MeshView) with explicit ranges
    (first_index/index_count or first_vertex/vertex_count) and/or embed those
    ranges into DrawMetadata.
  - Pros: Tighter, explicit low-level contract; avoids per-view duplication of
    material constants by reusing indices. Paves the way for GPU-driven
    submission.
  - Cons: Larger refactor now (new types, more plumbing). Requires pass/codegen
    changes and updated shaders if new metadata is consumed.

Decision: Implement Option A first for fast, low-risk enablement of per-submesh
draws. We can layer Option B later if profiling indicates benefits or when
moving to a full DrawPacket path.

Implementation notes for Option A (current):

- RenderItem gains: uint32 submesh_index.
- Finalize: copies submesh_index from collected data.
- RenderPass: For each item, iterate the selected SubMesh.MeshViews(); issue one
  draw per view.
  - Indexed meshes: Draw(index_count, 1, first_index, 0) and let the VS read the
    index buffer via SV_VertexID + bindless indirection.
  - Non-indexed meshes: Draw(vertex_count, 1, first_vertex, 0).
- Renderer::EnsureResourcesForDrawList: Append one DrawMetadata entry per issued
  draw so g_DrawIndex matches draw submission order; world transforms and
  material constants are duplicated per view for now (simple and correct).

---

## Implementation snapshot (August 2025)

This section captures what’s implemented now so the design stays consistent with
code. Future sections remain valid and are not removed.

- Builder API and scope
  - Class: `oxygen::engine::extraction::RenderListBuilder` in
    `Extraction/RenderListBuilder.{h,cpp}`
  - Methods: `Collect(scene, view, frame_id) -> std::vector<RenderItemData>`,
    `Finalize(span<RenderItemData>, RenderContext&, RenderItemsList&)`,
    `EvictStaleResources(...)`
  - Status: Collect + monolithic Finalize are implemented. Finalizers API is
    deferred to a later phase.

- Extractors and pipeline
  - Header-only extractors in `Extraction/Extractors_impl.h` with APIs in
    `Extraction/Extractors.h`:
    - `ShouldRenderPreFilter`, `TransformExtractor`, `NodeFlagsExtractor`,
      `MeshResolver` (LOD selection via scene policy), `VisibilityFilter`,
      `MaterialResolver` (no-op), `EmitPerVisibleSubmesh`.
  - Functional composition via `Extraction/Pipeline.h` which accepts a tuple of
    filter/updater/producer functions.
  - Collect pipeline used in code:
    - `ShouldRenderPreFilter -> TransformExtractor -> MeshResolver ->
      VisibilityFilter -> NodeFlagsExtractor -> MaterialResolver ->
      EmitPerVisibleSubmesh`

- Data types
  - `Extraction/RenderItemData.h` added for collect-phase snapshots (lod_index,
    submesh_index, geometry/material refs, world transform, flags).
  - `Renderer/Types/DrawMetadata.h` extended with per-view geometry slice
    fields: `first_index` and `base_vertex` (plus padding). Shaders updated
    accordingly.
  - `data::MeshView` now exposes `FirstIndex/IndexCount/FirstVertex/VertexCount`
    accessors used by the renderer.

- Renderer integration and draw behavior
  - Renderer uses `RenderListBuilder` inside `Renderer::BuildFrame(scene,
    view)`; it no longer calls legacy `SceneExtraction::CollectRenderItems`.
  - `RenderItem` extended with `submesh_index`, enabling per-submesh drawing
    (Option A).
  - `RenderPass::IssueDrawCalls` iterates the selected submesh’s `MeshViews()`
    and issues one draw per view. The draw index root constant is incremented
    per issued draw to match metadata order.
  - `Renderer::EnsureResourcesForDrawList` appends one DrawMetadata entry per
    view draw and duplicates material constants and world transform per view for
    correctness.

- Shaders alignment
  - HLSL files `DepthPrePass.hlsl` and `FullScreenTriangle.hlsl` updated to read
    `first_index` and `base_vertex` from `DrawMetadata` and compute the actual
    vertex index via bindless index buffer access.

- Tests
  - `RenderListBuilder_basic_test.cpp` covers smoke and distance-policy LOD
    selection per-view. CI artifact `renderlistbuilder_results.xml` shows
    passing tests.

Notes and limits right now

- Finalization is centralized in `RenderListBuilder::Finalize` (no pluggable
  finalizers yet).
- No transform residency/slots manager; transforms are copied directly from
  collected data into GPU buffers during ensure/upload.
- Sorting, batching keys, and residency/eviction policies are not yet integrated
  with the builder (renderer still performs mesh resource residency on demand).

---

## Extractors and Collect Pipeline Design

This section defines the extractor model, supporting types, default pipeline,
and contracts used during the Collect phase. It reflects the current implemented
path and remains compatible with future finalizers.

### Concepts and contracts

- ExtractorContext
  - Fields: `const View& view`, `scene::Scene& scene`, `uint64_t frame_id`.
  - Purpose: provide per-collect invocation data (camera/frustum, scene access,
    frame id).

- WorkItem
  - Lifecycle: constructed per `SceneNodeImpl` and flows through extractors.
  - Fields (collect-phase snapshot and transient state):
    - `RenderItemData proto` (seeded with geometry/material refs, world
      transform, bounds, flags)
    - `std::shared_ptr<const data::Mesh> mesh` (resolved active LOD mesh)
    - `std::optional<uint32_t> pending_lod` (selected LOD from policy)
    - `std::optional<uint32_t> selected_submesh` (unused in current default;
      per-submesh emission via mask)
    - `std::vector<char> submesh_mask` (per-submesh visibility)
    - `bool dropped` (early out marker)
  - Helpers: accessors to `Renderable()` and `Transform()` facades and `Node()`.

- Facades
  - RenderableFacade: forwards a minimal surface from `RenderableComponent` used
    by extractors
    - `UsesDistancePolicy/UsesScreenSpaceErrorPolicy`
    - `SelectActiveMesh(...)`, `GetActiveLodIndex()`
    - `IsSubmeshVisible(lod, submesh)`, `ResolveSubmeshMaterial(lod, submesh)`
    - `GetGeometry()`, `GetWorldBoundingSphere()`,
      `GetWorldSubMeshBoundingBox(i)`
  - TransformFacade: forwards `GetWorldMatrix()` from `TransformComponent`.

- Extractor function concepts
  - FilterFn: `bool f(WorkItem&, const ExtractorContext&)` — returns false to
    drop item and stop.
  - UpdaterFn: `void f(WorkItem&, const ExtractorContext&)` — mutate state,
    continue.
  - ProducerFn: `void f(WorkItem&, const ExtractorContext&, Collector)` — append
    one or more `RenderItemData` results; pipeline continues unless item is
    marked dropped.

### Pipeline composition

The compile-time `Pipeline<...>` accepts a sequence of extractor callables. Each
step is dispatched according to its concept (Filter/Updater/Producer). On first
drop, processing stops for that WorkItem. Producers can emit multiple items.

Default pipeline (implemented):

1) ShouldRenderPreFilter — requires visible node, Renderable + Transform
   present, and valid geometry; seeds `proto.geometry`.
2) TransformExtractor — copies world transform and world-space bounding sphere
   into `proto`.
3) MeshResolver — selects active LOD using the node’s policy (distance or SSE)
   and resolves `mesh` from `proto.geometry` and `lod`.
4) VisibilityFilter — builds `submesh_mask` for the resolved mesh, returns false
   if none visible.
5) NodeFlagsExtractor — copies effective flags (casts/receives shadows) into
   `proto`.
6) MaterialResolver — placeholder (no-op) kept for future preprocessing.
7) EmitPerVisibleSubmesh — for each mask-visible submesh, performs frustum
   culling and appends a `RenderItemData` with `lod_index`, `submesh_index`,
   `material`, `domain`, and cached transforms.

Extension points:

- The pipeline tuple is customizable per pass. Additional filters (e.g., layer
  masks), domain routing, or custom emitters can be composed without changing
  types.

### Data contracts and invariants

- RenderItemData (collect output)
  - Identity: `(lod_index, submesh_index[, view])` with asset refs
    (geometry/material) and cached transform/bounds/flags.
  - No GPU handles: strictly CPU-side references and values.

- LOD selection
  - Policy lives in `RenderableComponent`. Extractors call
    `SelectActiveMesh(...)` and read `GetActiveLodIndex()` to avoid duplicating
    hysteresis state outside the scene.

- Submesh emission
  - Current default emits per visible submesh. Multi-view slicing is handled
    later by iterating `MeshView`s at draw time.

### End-to-end collect flow

For each dense `SceneNodeImpl`:

- Construct `WorkItem` (caches component pointers in facades)
- Run the pipeline; if not dropped, producers append `RenderItemData` items to
  the output vector
- Return the vector to `RenderListBuilder::Finalize`

### Alignment with Finalize and Renderer

- Finalize (current): converts `RenderItemData` to immutable `RenderItem`,
  resolving the mesh pointer from `geometry->MeshAt(lod_index)` and copying
  `submesh_index` and flags.
- Renderer: iterates `RenderItem.mesh->SubMeshes()[submesh_index].MeshViews()`
  and issues one draw per view, with per-view `DrawMetadata` including
  `first_index` and `base_vertex` used in shaders.

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

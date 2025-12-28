# Bindless Conventions

This document captures the root binding layout and per-pass expectations in the
current bindless renderer implementation.

## Root bindings

The common root binding order is defined by the generated enum
`oxygen::engine::binding::RootParam` in `Generated.RootSignature.h`:

```cpp
enum class RootParam : uint32_t {
  kBindlessSrvTable = 0,   // descriptor table (SRVs) t0, space0
  kSamplerTable = 1,       // descriptor table (Samplers) s0, space0
  kSceneConstants = 2,     // direct CBV b1, space0
  kDrawIndex = 3,          // 32-bit root constant for draw index (b2, space0)
  kCount = 4,
};
```

## Current pass usage

All render passes use the complete root signature for consistency and simplicity:

| Pass | kBindlessSrvTable | kSamplerTable | kSceneConstants | kDrawIndex |
| -- | -- | -- | -- | -- |
| DepthPrePass | Yes | Yes | Yes | Yes |
| ShaderPass | Yes | Yes | Yes | Yes |
| TransparentPass | Yes | Yes | Yes | Yes |

**Notes:**

- All passes bind the same root signature (no per-pass customization).
- Passes may not use all bindings (e.g., DepthPrePass doesn't use material constants), but the root signature is shared.
- This design simplifies PSO creation and descriptor management.

## Multi-draw item support

The renderer now supports multiple draw items (meshes) in a single frame
through:

1. Root constant for draw index: each draw call receives a unique `draw_index`
   via root constant (b2, space0).
2. Per-draw binding: before each draw call, `BindDrawIndexConstant()` sets the
   current draw index.
3. Shader access: shaders use `g_DrawIndex` to index into per-draw arrays (e.g.,
   `DrawMetadata`, world transforms) exposed via the bindless table using slots
   carried in `SceneConstants`.

### Draw Index Flow

```cpp
// Per draw call in IssueDrawCalls():
BindDrawIndexConstant(command_recorder, draw_index);  // Set root constant
command_recorder.Draw(vertex_count, 1, 0, 0);        // Normal draw call
```

### Shader Usage

```hlsl
// Root constant declaration in shaders:
cbuffer DrawIndexConstant : register(b2, space0) {
  uint g_DrawIndex;
}

// Usage pattern in shader (illustrative):
//  - Get SRV slot for per-draw metadata from SceneConstants
//  - Read metadata for current draw using g_DrawIndex
//StructuredBuffer<DrawMetadata> draw_meta = ResourceDescriptorHeap[Scene.bindless_indices_slot];
//DrawMetadata dm = draw_meta[g_DrawIndex];
```

Note: Attempting to rely on `SV_InstanceID` with `firstInstance` does not work
reliably in D3D12 bindless scenarios when no input layout is used
(`pInputElementDescs = nullptr`). A 32-bit root constant is the recommended
approach for passing per-draw data.

## Descriptor allocation

Descriptor tables for bindless SRVs (structured buffers for per-draw metadata,
world transforms, material constants, vertex/index SRVs, etc.) are resident and
managed by backend systems (`RenderController` + descriptor allocator). Passes
set the pipeline state (establishes root signature), then bind `SceneConstants`
as a direct root CBV. All other resources are accessed via the bindless table.

## Mesh vertex/index SRV lifecycle

Creation (first ensure)

- The renderer creates vertex/index GPU buffers and registers their SRVs the
  first time `Renderer::EnsureMeshResources(const data::Mesh&)` is called for a
  mesh. Example code and passes must not allocate these SRVs directly.
- The shader-visible SRV indices are cached in `MeshGpuResources` and reused
  across frames. A one-time debug log summarizes assigned indices.

Reuse

- Subsequent calls to `EnsureMeshResources(mesh)` are cheap reuse checks; no
  per-frame descriptor creation occurs for resident meshes.
- Per-draw structured buffers record the current SRV indices each frame; shaders
  always read indices from those per-frame arrays, preventing stale bindings.

Eviction

- Backend caches may evict mesh resources (LRU). After eviction, the next
  `EnsureMeshResources(mesh)` recreates buffers and SRVs; indices can change.
  Because per-draw buffers are rebuilt each frame, new indices propagate
  automatically to shaders.

Invalid/absent

- `0xFFFFFFFFu` (kInvalidDescriptorSlot) denotes an unavailable descriptor slot;
  the renderer avoids emitting draw calls with invalid geometry SRVs.

## ScenePrep integration overview

The modern bindless rendering pipeline integrates tightly with the ScenePrep system:

**Collection Phase:**

- Scene graph traversal yields visible items with resolved LODs and materials.
- `RenderItemData` records carry geometry, material, and transform references.

**Finalization Phase:**

- **GeometryUploadFinalizer**: Ensures vertex/index buffers are resident; allocates bindless SRV indices.
- **TransformUploadFinalizer**: Uploads deduped world matrices; assigns bindless SRV index.
- **MaterialUploadFinalizer**: Uploads material constants; assigns bindless SRV index.
- **DrawMetadataEmitFinalizer**: Per-item: builds DrawMetadata with vertex/index/material SRV references.
- **DrawMetadataSortAndPartitionFinalizer**: Sorts and partitions draws by pass mask.
- **DrawMetadataUploadFinalizer**: Uploads metadata to GPU; assigns bindless SRV index.

**Frame Finalization:**

- All bindless SRV indices are captured in `SceneConstants` (via `ScenePrepState`).
- `PreparedSceneFrame` is populated with spans to finalized arrays and bindless slots.
- Render passes access `context_.current_view.prepared_frame` to iterate draws and bindless indices.

## SceneConstants (CBV b1, space0)

`SceneConstants` is a per-frame snapshot (view + timing + dynamic bindless slots) uploaded once per frame.
Defined in [Types/SceneConstants.h](../../Types/SceneConstants.h), the GPU layout (`SceneConstants::GpuData`) includes:

**Core Fields:**

- view_matrix: Camera view matrix (4x4)
- projection_matrix: Camera projection matrix (4x4)
- camera_position: World-space camera position (vec3)
- time_seconds: Elapsed time since engine start (float)
- frame_index: Current frame counter (uint32)

**Bindless Slot References:**

- bindless_worlds_slot: SRV slot for world transforms buffer (float4x4 per draw)
- bindless_normals_slot: SRV slot for normal transform matrices (inverse-transpose)
- bindless_materials_slot: SRV slot for material constants buffer
- bindless_draw_metadata_slot: SRV slot for draw metadata buffer

**Upload Timing:**

- Built fresh each frame during `Renderer::BuildFrame()` after view resolution.
- Uploaded to GPU during finalization phase.
- Bound in `RenderPass::BindSceneConstantsBuffer()` via direct GPU virtual address (root CBV).

**Lifetime:**

- Struct is 16-byte aligned for GPU efficiency.
- Renderer asserts `RenderContext.scene_constants` is null before wiring and clears it in `RenderContext::Reset()` at frame end.
- All bindless slots are set during `ScenePrepPipeline::Finalize()` after uploading the respective buffers.

## Material constants and texture binding

**MaterialBinder** (Renderer resource) manages materials via ScenePrep:

- **Per-material constants**: Each material asset maps to a `MaterialConstants` structure.
- **Deduplication**: Materials with identical shader constants reuse the same GPU slot (content-based hashing).
- **Upload**: Batched via `MaterialUploadFinalizer` during finalization.
- **Bindless access**: Material indices stored in `DrawMetadata.material_slot`; shaders read via `SceneConstants.bindless_materials_slot`.
- **Texture residency**: `TextureBinder` ensures textures are resident and provides bindless texture SRV indices.

**Pass usage:**

- **ShaderPass**: Reads material constants and textures for shading.
- **DepthPrePass**: Ignores material constants (depth-only rendering).
- **TransparentPass**: Reads material constants and textures (like ShaderPass).

## Constant and per-draw buffers summary

| Buffer | Purpose | Upload frequency | Root binding / Access |
| -- | -- | -- | -- |
| SceneConstants | View, timing, dynamic bindless slots | Once per frame (dirty) | CBV b1 space0 |
| MaterialConstants | Material snapshot(s) | 0 or 1+ per frame (opt.) | Structured SRV via bindless table (slot in SC) |
| DrawMetadata | Per-draw indices and config (vertex/index, flags, etc.) | Once per frame (dirty) | Structured SRV via bindless table (slot in SC) |
| WorldTransforms (float4x4) | Per-draw world matrices | Once per frame (dirty) | Structured SRV via bindless table (slot in SC) |

Notes:

- Slots live in `SceneConstants`; passes do not assume fixed heap indices.
- `0xFFFFFFFFu` (kInvalidDescriptorSlot) means “not available this frame”.
- Upload is deferred to `PreExecute` and performed only when data
  exists/changes.

## Per-draw structured buffers and access patterns

ScenePrep finalizers produce three key structured buffers (all dynamic, with slots in `SceneConstants`):

### DrawMetadata Buffer

- **One entry per visible submesh** (emission by `DrawMetadataEmitFinalizer`).
- **Contents** (see [Types/DrawMetadata.h](../../Types/DrawMetadata.h)):
  - Vertex/index buffer SRV indices from geometry
  - Material SRV index (from MaterialBinder)
  - Transform SRV index (from TransformUploader)
  - Flags (is_indexed, instance_count, first_index, base_vertex, vertex_count, etc.)
- **Access**: Via `SceneConstants.bindless_draw_metadata_slot` using `g_DrawIndex`.

### WorldTransforms Buffer

- **One float4x4 per draw** (uploaded by `TransformUploadFinalizer`).
- **Contents**: Deduplicated world transformation matrices with automatic normal matrix computation.
- **Access**: Via `SceneConstants.bindless_worlds_slot` using index from DrawMetadata or directly from `g_DrawIndex`.

### MaterialConstants Buffer

- **One entry per unique material** (uploaded by `MaterialUploadFinalizer`).
- **Contents**: Per-material shader constants (color, roughness, metallic, etc.).
- **Access**: Via `SceneConstants.bindless_materials_slot` using material index from DrawMetadata.

**Frame Update Protocol:**

1. **Collection Phase**: ScenePrep collects visible items with geometry/material/transform references.
2. **Finalization Phase**: Finalizers emit metadata, upload buffers, assign bindless SRV slots.
3. **Scene Constants Update**: All slots written to `SceneConstants` at finalization end.
4. **Pass Execution**: Each draw call sets `g_DrawIndex` via root constant before drawing.
5. **Shader Access**:

   ```hlsl
   // Get draw metadata
   StructuredBuffer<DrawMetadata> g_DrawMetadata = ResourceDescriptorHeap[Scene.bindless_draw_metadata_slot];
   DrawMetadata dm = g_DrawMetadata[g_DrawIndex];

   // Get world transform
   StructuredBuffer<float4x4> g_Worlds = ResourceDescriptorHeap[Scene.bindless_worlds_slot];
   float4x4 world = g_Worlds[dm.transform_index];

   // Get material constants
   StructuredBuffer<MaterialConstants> g_Materials = ResourceDescriptorHeap[Scene.bindless_materials_slot];
   MaterialConstants mat = g_Materials[dm.material_index];
   ```

**Multi-draw support:**

- Supports arbitrarily many draws per frame (structured buffers are growable).
- Each draw call sets a unique `g_DrawIndex` to select its metadata.
- No per-draw SRV binding overhead: all data accessed through fixed bindless table indices.

## Design patterns and best practices

**Minimize root signature churn:**

- All passes share the same root signature (4 root parameters).
- Avoid per-pass root signature variants; use dynamic slots in `SceneConstants` instead.
- If new data is needed, extend `SceneConstants` or add to bindless buffers.

**Layered descriptor management:**

- **Root parameters**: Stable across all passes; defined in `Generated.RootSignature.h`.
- **Bindless tables**: Assigned dynamically per frame; slots published via `SceneConstants`.
- **Draw index**: Only dynamic per-draw state; set via `BindDrawIndexConstant()`.

**For future extensions:**

- Additional per-pass data (e.g., light lists, shadow maps) should be added to bindless buffers or new structured buffers (with slots in `SceneConstants`), not new root parameters.
- If adding new root parameters, append them to maintain index stability for existing code.
- Consider separating geometry / material / sampler spaces only if shader conventions require it (unlikely in modern bindless).

## Related Documentation

- [Scene Preparation Pipeline](scene_prep.md) - Collection and finalization phases
- [Render Pass Lifecycle](render_pass_lifecycle.md) - How passes integrate with the system
- [Draw Metadata](render_items.md) - Per-draw record layout and access patterns
- [Render Graph Patterns](render_graph_patterns.md) - Coroutine-based pass orchestration

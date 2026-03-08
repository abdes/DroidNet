# Bindless Conventions

This document captures the root binding layout and per-pass expectations in the
current bindless renderer implementation.

Target architecture note:

- This document describes the current implementation ABI.
- The target post-refactor architecture is defined in
  [renderer_shader_interface_refactor.md](renderer_shader_interface_refactor.md).
- Phase 1 now routes environment, lighting, debug, and view-color data through
  `ViewFrameBindings`. There is no dedicated `b3` environment root CBV in the
  live ABI.

## Root bindings

The common root binding order is defined by the generated enum
`oxygen::engine::binding::RootParam` in `Generated.RootSignature.h`:

```cpp
enum class RootParam : uint32_t {
  kBindlessSrvTable = 0,   // descriptor table (SRVs) t0, space0
  kSamplerTable = 1,       // descriptor table (Samplers) s0, space0
  kViewConstants = 2,      // direct CBV b1, space0
  kRootConstants = 3,      // two 32-bit root constants at b2, space0
  kCount = 4,
};
```

## Current pass usage

All engine-owned render passes use the complete generated root signature for
consistency and simplicity:

| Pass family | kBindlessSrvTable | kSamplerTable | kViewConstants | kRootConstants |
| -- | -- | -- | -- | -- |
| Graphics passes | Yes | Yes | Yes | Yes |
| Compute passes | Yes | Yes | Yes | Yes |

**Notes:**

- All passes bind the same root signature (no per-pass customization).
- Passes may not use all bindings, but the root signature is shared.
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
   carried in `ViewConstants`.

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
//  - Resolve DrawFrameBindings through ViewFrameBindings
//  - Get SRV slot for per-draw metadata from DrawFrameBindings
//  - Read metadata for current draw using g_DrawIndex
//DrawFrameBindings draw = LoadResolvedDrawFrameBindings();
//StructuredBuffer<DrawMetadata> draw_meta = ResourceDescriptorHeap[draw.draw_metadata_slot];
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
set the pipeline state (establishes root signature), then bind:

- `ViewConstants` as a direct root CBV at `b1`
- `RootConstants` at `b2`

All other extensible resources are accessed via the bindless table.

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

- Draw-system bindless SRV indices are captured in `PreparedSceneFrame`.
- Renderer publishes `DrawFrameBindings` for the active view and routes it
  through `ViewFrameBindings.draw_frame_slot`.
- `PreparedSceneFrame` is populated with spans to finalized arrays and bindless slots.
- Render passes access `context_.current_view.prepared_frame` to iterate draws and bindless indices.

## ViewConstants (CBV b1, space0)

`ViewConstants` is the current per-view root CBV snapshot uploaded once per
view. Defined in [Types/ViewConstants.h](../../Types/ViewConstants.h), the GPU
layout (`ViewConstants::GpuData`) is currently 256 bytes and includes:

**Core Fields:**

- frame sequence / frame slot / time
- view_matrix: Camera view matrix (4x4)
- projection_matrix: Camera projection matrix (4x4)
- camera_position: World-space camera position (vec3)

**Bindless Slot References:**

- bindless_view_frame_bindings_slot: SRV slot for the top-level
  `ViewFrameBindings` routing payload

**Upload Timing:**

- Built fresh each frame during `Renderer::BuildFrame()` after view resolution.
- Uploaded to GPU during finalization phase.
- Bound in `RenderPass::BindViewConstantsBuffer()` via direct GPU virtual
  address (root CBV).

**Lifetime:**

- Struct is 16-byte aligned for GPU efficiency.
- Struct is padded to 256 bytes for root-CBV safety and ABI stability.
- Renderer asserts `RenderContext.view_constants` is null before wiring and
  clears it in `RenderContext::Reset()` at frame end.
- System-owned frame bindings are published per view during
  `Renderer::PrepareAndWireViewConstantsForView()`.

## Material constants and texture binding

**MaterialBinder** (Renderer resource) manages materials via ScenePrep:

- **Per-material constants**: Each material asset maps to a `MaterialConstants` structure.
- **Deduplication**: Materials with identical shader constants reuse the same GPU slot (content-based hashing).
- **Upload**: Batched via `MaterialUploadFinalizer` during finalization.
- **Bindless access**: Material indices stored in `DrawMetadata.material_handle`;
  shaders resolve `DrawFrameBindings` and read via
  `DrawFrameBindings.material_constants_slot`.
- **Texture residency**: `TextureBinder` ensures textures are resident and provides bindless texture SRV indices.

**Pass usage:**

- **ShaderPass**: Reads material constants and textures for shading.
- **DepthPrePass**: Ignores material constants (depth-only rendering).
- **TransparentPass**: Reads material constants and textures (like ShaderPass).

## Constant and per-draw buffers summary

| Buffer | Purpose | Upload frequency | Root binding / Access |
| -- | -- | -- | -- |
| ViewConstants | Current per-view invariants and routing slots | Once per view (dirty) | CBV b1 space0 |
| MaterialConstants | Material snapshot(s) | 0 or 1+ per frame (opt.) | Structured SRV via bindless table (slot in `DrawFrameBindings`) |
| DrawMetadata | Per-draw indices and config (vertex/index, flags, etc.) | Once per frame (dirty) | Structured SRV via bindless table (slot in `DrawFrameBindings`) |
| WorldTransforms (float4x4) | Per-draw world matrices | Once per frame (dirty) | Structured SRV via bindless table (slot in `DrawFrameBindings`) |

Notes:

- Draw-system slots live in `DrawFrameBindings`; passes do not assume fixed
  heap indices.
- `0xFFFFFFFFu` (kInvalidDescriptorSlot) means “not available this frame”.
- Upload is deferred to `PreExecute` and performed only when data
  exists/changes.

## ViewFrameBindings

`ViewFrameBindings` is now published as a bindless structured-buffer payload and
its SRV slot is carried in `ViewConstants.bindless_view_frame_bindings_slot`.

Current status:

- It is the first replacement routing contract toward the final interface
  model.
- The payload currently contains top-level system slots only:
  - draw
  - lighting
  - environment
  - view color
  - shadow
  - post-process
  - debug
  - history
  - ray tracing
- `draw` is now a live migrated system slot and routes `DrawFrameBindings` for
  draw metadata, world transforms, normal matrices, material constants, and
  instance data.
- `debug` is now a live migrated system slot and routes `DebugFrameBindings`.
- `lighting` is now a live migrated system slot and routes
  `LightingFrameBindings` for light arrays, canonical sun, and clustered-light
  state.
- `environment` is now a live migrated system slot and routes
  `EnvironmentFrameBindings` for both static environment resources and
  environment-owned per-view view data.
- `view color` is now a live migrated system slot and routes `ViewColorData`
  for shared exposure.
- Remaining system slots may still be invalid placeholders until the
  corresponding system contracts are migrated.
- The legacy direct environment/light/debug/exposure slots were removed from
  `ViewConstants` in the live ABI.

Practical rule:

- New interface work should prefer routing through `ViewFrameBindings` rather
  than adding new responsibilities to `ViewConstants`.

## Per-draw structured buffers and access patterns

ScenePrep finalizers produce three key structured buffers (all dynamic, with
slots published in `DrawFrameBindings`):

### DrawFrameBindings

- **One element per view** published by the renderer after scene preparation.
- **Contents** (see [Types/DrawFrameBindings.h](../../Types/DrawFrameBindings.h)):
  - `draw_metadata_slot`
  - `transforms_slot`
  - `normal_matrices_slot`
  - `material_constants_slot`
  - `instance_data_slot`
- **Access**: Via `ViewFrameBindings.draw_frame_slot`, usually through
  `LoadResolvedDrawFrameBindings()`.

### DrawMetadata Buffer

- **One entry per visible submesh** (emission by `DrawMetadataEmitFinalizer`).
- **Contents** (see [Types/DrawMetadata.h](../../Types/DrawMetadata.h)):
  - Vertex/index buffer SRV indices from geometry
  - Material SRV index (from MaterialBinder)
  - Transform SRV index (from TransformUploader)
  - Flags (is_indexed, instance_count, first_index, base_vertex, vertex_count, etc.)
- **Access**: Via `DrawFrameBindings.draw_metadata_slot` using `g_DrawIndex`.

### WorldTransforms Buffer

- **One float4x4 per draw** (uploaded by `TransformUploadFinalizer`).
- **Contents**: Deduplicated world transformation matrices with automatic normal matrix computation.
- **Access**: Via `DrawFrameBindings.transforms_slot` using index from
  DrawMetadata or directly from `g_DrawIndex`.

### MaterialConstants Buffer

- **One entry per unique material** (uploaded by `MaterialUploadFinalizer`).
- **Contents**: Per-material shader constants (color, roughness, metallic, etc.).
- **Access**: Via `DrawFrameBindings.material_constants_slot` using
  material index from DrawMetadata.

**Frame Update Protocol:**

1. **Collection Phase**: ScenePrep collects visible items with geometry/material/transform references.
2. **Finalization Phase**: Finalizers emit metadata, upload buffers, assign bindless SRV slots.
3. **View Routing Update**: Renderer publishes `DrawFrameBindings` and stores
   its SRV slot in `ViewFrameBindings.draw_frame_slot`.
4. **Pass Execution**: Each draw call sets `g_DrawIndex` via root constant before drawing.
5. **Shader Access**:

   ```hlsl
   DrawFrameBindings draw = LoadResolvedDrawFrameBindings();

   // Get draw metadata
   StructuredBuffer<DrawMetadata> g_DrawMetadata = ResourceDescriptorHeap[draw.draw_metadata_slot];
   DrawMetadata dm = g_DrawMetadata[g_DrawIndex];

   // Get world transform
   StructuredBuffer<float4x4> g_Worlds = ResourceDescriptorHeap[draw.transforms_slot];
   float4x4 world = g_Worlds[dm.transform_index];

   // Get material constants
   StructuredBuffer<MaterialConstants> g_Materials = ResourceDescriptorHeap[draw.material_constants_slot];
   MaterialConstants mat = g_Materials[dm.material_handle];
   ```

**Multi-draw support:**

- Supports arbitrarily many draws per frame (structured buffers are growable).
- Each draw call sets a unique `g_DrawIndex` to select its metadata.
- No per-draw SRV binding overhead: all data accessed through fixed bindless table indices.

## Design patterns and best practices

**Minimize root signature churn:**

- All passes share the same generated root signature (currently 4 root
  parameters).
- Avoid per-pass root signature variants.
- New interface work should follow
  [renderer_shader_interface_refactor.md](renderer_shader_interface_refactor.md),
  not further expand `ViewConstants`.

**Layered descriptor management:**

- **Root parameters**: Stable across all passes; defined in `Generated.RootSignature.h`.
- **Bindless tables**: Assigned dynamically per frame; slots published via
  `ViewConstants`.
- **Root constants**: `g_DrawIndex` is set per draw; `g_PassConstantsIndex` is
  set per pass.

**For future extensions:**

- Additional per-pass or per-system data should not be added as new root
  parameters.
- If adding new root parameters, append them to maintain index stability for existing code.
- Consider separating geometry / material / sampler spaces only if shader conventions require it (unlikely in modern bindless).

## Related Documentation

- [Scene Preparation Pipeline](scene_prep.md) - Collection and finalization phases
- [Render Pass Lifecycle](render_pass_lifecycle.md) - How passes integrate with the system
- [Draw Metadata](render_items.md) - Per-draw record layout and access patterns
- [Render Graph Patterns](render_graph_patterns.md) - Coroutine-based pass orchestration

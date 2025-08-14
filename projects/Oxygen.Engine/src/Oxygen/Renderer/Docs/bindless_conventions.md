# Bindless Conventions

This document captures the root binding layout and per-pass expectations in the
current bindless renderer implementation.

## Root bindings

The common root binding order used by passes is defined in
`RenderPass::RootBindings`:

```text
enum class RenderPass::RootBindings : uint8_t {
  kBindlessTableSrv = 0,   // descriptor table (SRVs) t0, space0
  kSceneConstantsCbv = 1,  // direct CBV b1, space0
  kDrawIndexConstant = 2,  // 32-bit root constant for draw index (b3, space0)
};
```

## Current pass usage

| Pass        | kBindlessTableSrv | kSceneConstantsCbv | kDrawIndexConstant |
|-------------|-------------------|--------------------|--------------------|
| DepthPrePass| Yes               | Yes                | Yes                |
| ShaderPass  | Yes               | Yes                | Yes                |

## Multi-draw item support

The renderer now supports multiple draw items (meshes) in a single frame
through:

1. Root constant for draw index: each draw call receives a unique `draw_index`
   via root constant (b3, space0).
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
[[vk::push_constant]]
cbuffer DrawIndexConstants : register(b3, space0) {
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

## Eviction status (current)

- Policy: LRU policy exists and `Renderer::EvictUnusedMeshResources(frame)`
  removes entries from `mesh_resources_` and notifies the policy via
  `OnMeshRemoved`.
- Buffers: GPU buffers are owned by `std::shared_ptr`; erasing the cache entry
  releases buffers when no other references remain.
- Views/Descriptors: SRV views registered via
  `ResourceRegistry::RegisterView(...)` are not explicitly unregistered on
  eviction/unregister yet. This can leave descriptor table entries allocated
  until process teardown.
- Integration: A periodic call to `EvictUnusedMeshResources` is not yet wired
  into the frame loop.

Planned follow‑up (Phase 9):

- On eviction/unregister, call
  `ResourceRegistry::UnRegisterResource(*vertex_buffer)` and
  `UnRegisterResource(*index_buffer)` to drop all views and free descriptors.
- Integrate `EvictUnusedMeshResources(current_frame)` into frame orchestration
  (e.g., `PostExecute`).
- Add metrics (evicted meshes, freed descriptors) and unit tests around
  unregistration.

## SceneConstants (CBV b1, space0)

`SceneConstants` is a per-frame snapshot (view + timing + dynamic slots) that
the `Renderer` builds and uploads once per frame during `PreExecute`. The GPU
layout is `SceneConstants::GpuData`:

- view_matrix, projection_matrix, camera_position, time_seconds, frame_index
- bindless_indices_slot: SRV slot for per-draw metadata (current impl)
- bindless_draw_metadata_slot: reserved for future decoupling
- bindless_transforms_slot: SRV slot for world matrices buffer
- bindless_material_constants_slot: SRV slot for material constants buffer

General notes:

- Object/world transforms remain per-item (not part of `SceneConstants`).
- Struct is 16-byte aligned by design; see `Types/SceneConstants.h`.
- The renderer asserts `RenderContext.scene_constants` is null before wiring and
  clears it in `RenderContext::Reset` at the end of the frame.

## Material constants (bindless SRV)

`MaterialConstants` is provided via a bindless structured buffer managed by the
renderer (not a direct CBV). When `Renderer::SetMaterialConstants` is called,
the CPU snapshot is stored (typically a single element) and uploaded during
`PreExecute` if present. The SRV heap slot is written into
`SceneConstants.bindless_material_constants_slot`.

Passes that rely on material shading (e.g., `ShaderPass`) read from the bindless
table. `DepthPrePass` ignores it.

## Constant and per-draw buffers summary

| Buffer                    | Purpose                                                | Upload frequency         | Root binding / Access                              |
|---------------------------|--------------------------------------------------------|--------------------------|----------------------------------------------------|
| SceneConstants            | View, timing, dynamic bindless slots                   | Once per frame (dirty)   | CBV b1 space0                                      |
| MaterialConstants         | Material snapshot(s)                                   | 0 or 1+ per frame (opt.) | Structured SRV via bindless table (slot in SC)     |
| DrawMetadata              | Per-draw indices and config (vertex/index, flags, etc.)| Once per frame (dirty)   | Structured SRV via bindless table (slot in SC)     |
| WorldTransforms (float4x4)| Per-draw world matrices                                | Once per frame (dirty)   | Structured SRV via bindless table (slot in SC)     |

Notes:

- Slots live in `SceneConstants`; passes do not assume fixed heap indices.
- `0xFFFFFFFFu` (kInvalidDescriptorSlot) means “not available this frame”.
- Upload is deferred to `PreExecute` and performed only when data
  exists/changes.

## Per-draw structured buffers (dynamic bindless slots)

The renderer uploads per-draw data to structured buffers and exposes them via
dynamic slots stored in `SceneConstants`. Two key buffers:

- DrawMetadata: one entry per draw containing indices into vertex/index SRVs,
  flags (e.g., `is_indexed`), instance count, transform offset, and more. See
  `Types/DrawMetadata.h` for the authoritative layout.
- WorldTransforms: one `float4x4` per draw (submission order), indexed by
  `DrawMetadata.transform_offset` or directly by `g_DrawIndex` depending on
  shader.

Update protocol:

- Renderer ensures mesh SRVs exist (vertex/index) and caches their heap indices.
- `EnsureResourcesForDrawList()` builds `DrawMetadata` and world-transform
  arrays in submission order and marks them dirty.
- During `PreExecute`, the boundless helpers upload CPU snapshots (if present)
  and assign SRV slots; the renderer writes the slots into `SceneConstants`.
- Each draw call binds the correct `g_DrawIndex` via `BindDrawIndexConstant()`.

Shader access pattern (illustrative):

```hlsl
// Per-draw metadata
StructuredBuffer<DrawMetadata> draw_meta = ResourceDescriptorHeap[Scene.bindless_indices_slot];
DrawMetadata dm = draw_meta[g_DrawIndex];

// World transforms
StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[Scene.bindless_transforms_slot];
float4x4 world = worlds[dm.transform_offset];
```

Limitations:

- Multiple meshes per frame are supported; one entry per drawn item.
- Root constant must be set before each draw for correct indexing.

## Future extensions

- Additional root parameters for per-pass dynamic descriptors (e.g., light
  lists) should be appended (maintain ordering stability for existing indices).
- Additional per-pass constant ranges should prefer extending existing snapshot
  structs (scene/material) before introducing new root parameters (limits root
  signature churn).
- Consider separating geometry / material / sampler spaces if shader conventions
  evolve.

Related: [render pass lifecycle](render_pass_lifecycle.md), [multi-draw
implementation](multi_draw_implementation.md),
[ShaderPass](passes/shader_pass.md), [DepthPrePass](passes/depth_pre_pass.md).

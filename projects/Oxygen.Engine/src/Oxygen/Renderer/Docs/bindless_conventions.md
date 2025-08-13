# Bindless Conventions

Documents the root binding layout and usage expectations adopted by current
passes.

## Root Bindings Enumeration

```text
enum class RenderPass::RootBindings : uint8_t {
  kBindlessTableSrv = 0,      // descriptor table (SRVs) t0 space0
  kSceneConstantsCbv = 1,     // direct CBV b1 space0
  kMaterialConstantsCbv = 2,  // direct CBV b2 space0 (material snapshot; provided when material shading required)
  kDrawIndexConstant = 3,     // root constant for draw index (32-bit value, b3 space0)
};
```

## Current Pass Usage

| Pass | Uses kBindlessTableSrv | Uses kSceneConstantsCbv | Uses kMaterialConstantsCbv | Uses kDrawIndexConstant |
|------|------------------------|--------------------------|----------------------------|-------------------------|
| DepthPrePass | Yes | Yes | Yes* | Yes |
| ShaderPass | Yes | Yes | Yes | Yes |

*DepthPrePass includes MaterialConstants binding for root signature consistency but does not use it.

## Multi-Draw Item Support

The renderer now supports multiple draw items (meshes) in a single frame through:

1. **Root Constants for Draw Index**: Each draw call receives a unique `draw_index` via root constant (b3, space0)
2. **Per-Draw Resource Binding**: Before each draw call, `BindDrawIndexConstant()` sets the current draw index
3. **Shader Access Pattern**: Shaders use `g_DrawIndex` root constant to index into `DrawResourceIndices` array

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

// Usage in vertex shader:
DrawResourceIndices drawRes = g_DrawResourceIndices[g_DrawIndex];
```

**Note**: Previous attempts to use `SV_InstanceID` with `firstInstance` parameter failed in D3D12 bindless rendering because the pipeline has no input layout (`pInputElementDescs = nullptr`), bypassing the input assembler where `SV_InstanceID` is generated. Root constants provide the Microsoft-recommended solution for passing per-draw data in bindless scenarios.

## Descriptor Allocation

Descriptor tables for bindless SRVs (structured buffers for draw resource
indices / vertex & index buffers in the current design) are assumed resident and
managed by backend systems (`RenderController` + descriptor allocator). Passes
rely on setting the pipeline state which establishes the root signature, then
they set root CBVs via direct GPU virtual address.

## Scene Constants (b1, space0)

`SceneConstants` is a per-frame snapshot (view + frame timing) uploaded once by
the `Renderer` in `PreExecute` when marked dirty via
`Renderer::SetSceneConstants`. Callers no longer inject a GPU buffer into the
`RenderContext` directly; they only provide CPU data.

Layout (Phase 1 current implementation):

```c++
struct SceneConstants {
  glm::mat4 view_matrix;                // camera view matrix
  glm::mat4 projection_matrix;          // camera projection matrix
  glm::vec3 camera_position; float time_seconds;
  uint32_t frame_index;                 // monotonic frame counter
  uint32_t bindless_indices_slot;  // shader-visible SRV heap slot for DrawResourceIndices (0xFFFFFFFFu when unavailable)
  uint32_t _reserved[2];                // alignment / future fields
};
static_assert(sizeof(SceneConstants) % 16 == 0);
```

Notes:

* Object/world transforms are per-item (excluded here).
* Must remain 16-byte size-aligned; append new fields before `_reserved`.
* Exactly one upload per frame (last-wins if multiple `SetSceneConstants`).
* Renderer asserts `RenderContext.scene_constants` is null before injection.
* Renderer clears injected buffer pointers (`scene_constants`, `material_constants`) in `RenderContext::Reset`.

## Material Constants (b2, space0)

`MaterialConstants` is a per-frame (current material selection) snapshot.
The example sets it once per frame using the first (and only) item's material.
When provided via `Renderer::SetMaterialConstants` it is uploaded just-in-time
in `PreExecute` (only if the CPU copy differs from previous frame – memcmp
dirty check). Passes that rely on material shading (e.g., `ShaderPass`)
consume it; passes like `DepthPrePass` ignore it.

Layout mirrors HLSL cbuffer `MaterialConstants` (see `MaterialConstants.h`).
Padding uses explicit `uint _pad0`, `_pad1` to maintain 16-byte alignment.
Renderer clears the injected pointer in `RenderContext::Reset` along with
scene constants.

## Constant & Indices Buffer Layout Summary

| Buffer | Purpose | Upload Frequency | Root Binding / Access |
|--------|---------|------------------|------------------------|
| SceneConstants | View & frame state + dynamic bindless_indices_slot | Once per frame (dirty) | CBV b1 space0 |
| MaterialConstants | Current material snapshot (optional) | 0 or 1 per frame (dirty) | CBV b2 space0 |
| DrawResourceIndices | Vertex/index buffer descriptor indices + indexed flag | 0+ per frame (dirty snapshot) | Structured SRV in bindless table (slot varies) |

Notes:

* `bindless_indices_slot` lives inside `SceneConstants`, avoiding a fixed slot assumption.
* A value of `0xFFFFFFFFu` means the structured buffer was not provided; shaders must branch.
* Dirty tracking: memcmp against prior CPU snapshot; GPU upload deferred until `PreExecute`.

## DrawResourceIndices Structured Buffer (Dynamic Bindless Slot)

The `DrawResourceIndices` structured buffer holds the mapping from each draw's
vertex & index buffers to their shader-visible descriptor heap indices plus an
`is_indexed` flag. **The renderer now supports multiple draw items per frame**,
with each entry in the array corresponding to a different mesh/draw call.

The actual SRV slot is written each frame into `SceneConstants.bindless_indices_slot`.
Shaders access the appropriate entry using the draw index passed via root constant:
`g_DrawResourceIndices[g_DrawIndex]`. A `bindless_indices_slot` value of `0xFFFFFFFF`
indicates the buffer is not available (no geometry this frame).

Layout (12 bytes per draw item):

```c++
struct DrawResourceIndices {
  uint32_t vertex_buffer_index; // heap index of vertex buffer SRV
  uint32_t index_buffer_index;  // heap index of index buffer SRV
  uint32_t is_indexed;          // 1 = indexed draw, 0 = non-indexed
};
static_assert(sizeof(DrawResourceIndices)==12);
```

Update Protocol (Current Implementation):

* Renderer creates SRVs for mesh vertex/index buffers and caches their indices
* `EnsureResourcesForDrawList()` builds a `DrawResourceIndices` array with one entry per draw item
* Array is uploaded to a structured buffer, and the SRV slot is stored in `SceneConstants.bindless_indices_slot`
* Each draw call uses `BindDrawIndexConstant()` to set the current index before drawing
* Shaders use `g_DrawIndex` root constant to access the correct array entry

Multi-Draw Shader Access Pattern:

```hlsl
// Get resources for current draw:
DrawResourceIndices drawRes = g_DrawResourceIndices[g_DrawIndex];

// Access vertex data:
StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[drawRes.vertex_buffer_index];
StructuredBuffer<uint> indices = ResourceDescriptorHeap[drawRes.index_buffer_index];
```

Limitations:

* **Resolved**: Previous single-item limitation removed; now supports multiple meshes per frame
* Each draw item must have its own entry in the `DrawResourceIndices` array
* Root constant binding required before each draw call for proper indexing

## Future Extensions

* Additional root parameters for per-pass dynamic descriptors (e.g., light
  lists) should be appended (maintain ordering stability for existing indices).
* Additional per-pass constant ranges should prefer extending existing
  snapshot structs (scene/material) before introducing new root parameters
  (limits root signature churn).
* Consider separating geometry / material / sampler spaces if shader conventions
  evolve.

Related: [render pass lifecycle](render_pass_lifecycle.md),
[ShaderPass](passes/shader_pass.md), [DepthPrePass](passes/depth_pre_pass.md).

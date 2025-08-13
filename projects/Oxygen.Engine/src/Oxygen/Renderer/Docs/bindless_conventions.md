# Bindless Conventions

Documents the root binding layout and usage expectations adopted by current
passes.

## Root Bindings Enumeration

```text
enum class RenderPass::RootBindings : uint8_t {
  kBindlessTableSrv = 0,      // descriptor table (SRVs) t0 space0
  kSceneConstantsCbv = 1,     // direct CBV b1 space0
  kMaterialConstantsCbv = 2,  // direct CBV b2 space0 (material snapshot; provided when material shading required)
};
```

## Current Pass Usage

| Pass | Uses kBindlessTableSrv | Uses kSceneConstantsCbv | Uses kMaterialConstantsCbv |
|------|------------------------|--------------------------|----------------------------|
| DepthPrePass | Yes | Yes | No |
| ShaderPass | Yes | Yes | Yes |

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
  uint32_t draw_resource_indices_slot;  // shader-visible SRV heap slot for DrawResourceIndices (0xFFFFFFFFu when unavailable)
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
| SceneConstants | View & frame state + dynamic draw_resource_indices_slot | Once per frame (dirty) | CBV b1 space0 |
| MaterialConstants | Current material snapshot (optional) | 0 or 1 per frame (dirty) | CBV b2 space0 |
| DrawResourceIndices | Vertex/index buffer descriptor indices + indexed flag | 0+ per frame (dirty snapshot) | Structured SRV in bindless table (slot varies) |

Notes:

* `draw_resource_indices_slot` lives inside `SceneConstants`, avoiding a fixed slot assumption.
* A value of `0xFFFFFFFFu` means the structured buffer was not provided; shaders must branch.
* Dirty tracking: memcmp against prior CPU snapshot; GPU upload deferred until `PreExecute`.

## DrawResourceIndices Structured Buffer (Dynamic Bindless Slot)

The `DrawResourceIndices` structured buffer holds the mapping from the current
draw's vertex & index buffers to their shader-visible descriptor heap indices
plus an `is_indexed` flag. Earlier revisions relied on the descriptor being at
heap slot 0; that brittle ordering assumption has been removed. The actual SRV
slot is written each frame into `SceneConstants.draw_resource_indices_slot`.
Shaders must read this slot and index the bindless table dynamically. A value
of `0xFFFFFFFF` indicates the buffer is not available (no geometry this frame)
and shaders must branch accordingly.

Layout (12 bytes):

```c++
struct DrawResourceIndices {
  uint32_t vertex_buffer_index; // heap index of vertex buffer SRV
  uint32_t index_buffer_index;  // heap index of index buffer SRV
  uint32_t is_indexed;          // 1 = indexed draw, 0 = non-indexed
};
static_assert(sizeof(DrawResourceIndices)==12);
```

Update Protocol:

* Application (or future internal mesh system) calls
  `Renderer::SetDrawResourceIndices(indices)` (last-wins).
* Renderer allocates a structured buffer + SRV on first use, uploads when
  dirty.
* Renderer writes the SRV's heap slot into
  `SceneConstants.draw_resource_indices_slot` prior to uploading the scene
  constants buffer for the frame.
* Shaders fetch the slot: `uint slot = Scene.draw_resource_indices_slot;`
  and then conditionally access `g_DrawResourceIndices[0]` via that slot
  indirection (implementation dependent). A slot of `0xFFFFFFFF` means skip.

Future Phases:

* A subsequent phase relocates per-mesh SRV allocation & indices derivation to
  automated packet build removing any need for an external caller to set the
  snapshot.
* Later (DrawPacket introduction) the buffer may evolve into a broader geometry
  indirection table or be superseded entirely.

Limitations:

* Single global snapshot – multiple meshes with distinct buffers require updating the snapshot between draws (Phase 1 acceptable constraint; will be removed in DrawPacket phase).

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

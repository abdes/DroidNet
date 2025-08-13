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

Layout (Phase 1):

```c++
struct SceneConstants {
  mat4 view_matrix;
  mat4 projection_matrix;
  vec3 camera_position; float time_seconds;
  uint frame_index; uint _reserved[3]; // padding / future expansion
};
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

# Bindless Conventions

Documents the root binding layout and usage expectations adopted by current
passes.

## Root Bindings Enumeration

```text
enum class RenderPass::RootBindings : uint8_t {
  kBindlessTableSrv = 0,      // descriptor table (SRVs) t0 space0
  kSceneConstantsCbv = 1,     // direct CBV b1 space0
  kMaterialConstantsCbv = 2,  // direct CBV b2 space0 (optional)
};
```

## Current Pass Usage

| Pass | Uses kBindlessTableSrv | Uses kSceneConstantsCbv | Uses kMaterialConstantsCbv |
|------|------------------------|--------------------------|----------------------------|
| DepthPrePass | Yes | Yes | No |
| ShaderPass | Yes | Yes | Conditional (if material constants buffer present) |

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
* Renderer clears the injected buffer pointer in `RenderContext::Reset`; only
  `scene_constants` is renderer-owned in Phase 1 (material constants remain
  caller-managed until a dedicated API is added).

## Future Extensions

* Additional root parameters for per-pass dynamic descriptors (e.g., light
  lists) should be appended (maintain ordering stability for existing indices).
* Material constants will transition to the same snapshot pattern via
  `Renderer::SetMaterialConstants` (future Phase 1 task); until then the caller
  may populate `RenderContext.material_constants` directly.
* Consider separating geometry / material / sampler spaces if shader conventions
  evolve.

Related: [render pass lifecycle](render_pass_lifecycle.md),
[ShaderPass](passes/shader_pass.md), [DepthPrePass](passes/depth_pre_pass.md).

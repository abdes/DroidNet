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

## Future Extensions

* Additional root parameters for per-pass dynamic descriptors (e.g., light
  lists) should be appended (maintain ordering stability for existing indices).
* Consider separating geometry / material / sampler spaces if shader conventions
  evolve.

Related: [render pass lifecycle](render_pass_lifecycle.md),
[ShaderPass](passes/shader_pass.md), [DepthPrePass](passes/depth_pre_pass.md).

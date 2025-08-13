# Data Flow & Pass IO Summary

Summarises inputs/outputs of implemented and placeholder passes without
duplicating pipeline details found elsewhere.

## Implemented Passes

### DepthPrePass

* Inputs: `RenderContext::opaque_draw_list`, `scene_constants`, depth texture.
* Outputs: populated depth texture.

### ShaderPass

* Inputs: depth texture (read-only if present), `opaque_draw_list`,
  `scene_constants`, optional `material_constants`, color texture.
* Outputs: color render target (first color attachment or override texture).

## Planned / Placeholder (Not Implemented Yet)

These appear in historic docs but are not present in code today:

* LightCullingPass (would read depth, lights; output light lists / grid)
* TransparentPass (would read opaque color + depth; output accumulation /
  resolve)
* PostProcessPass (would read previous color; output final tone-mapped buffer)
* DebugOverlayPass (would composite overlays onto final color)

## Resource Ownership Reminder

Textures / buffers referenced above are owned via `ResourceRegistry` /
`RenderController`; passes only transition and bind them.

Related: [bindless conventions](../bindless_conventions.md), [render pass
lifecycle](../render_pass_lifecycle.md).

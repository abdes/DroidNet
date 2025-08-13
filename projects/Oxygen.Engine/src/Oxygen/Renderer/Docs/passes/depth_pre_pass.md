# DepthPrePass

Focus: populate depth buffer early for later passes and (future) light culling;
minimize overdraw.

## Configuration

`DepthPrePassConfig` currently holds:

* `depth_texture` (required)
* `debug_name`

Framebuffer reference is indirect via `RenderContext::framebuffer` (if present
and consistent with config depth texture). Viewport / scissors / clear color are
stored directly on the pass (optional, default derived from depth texture size).

## Pipeline State

Rebuilt when depth texture format or sample count changes (see
`NeedRebuildPipelineState`). Uses depth-only framebuffer layout (no color
targets) and culls back faces (`CullMode::kBack`). **Root signature includes
MaterialConstants binding for consistency with other passes but does not use it.**
Supports multi-draw items through draw index root constant.

## Resource Preparation

Transitions depth texture to `kDepthWrite` then flushes barriers. View /
descriptor creation of DSV occurs during execution helper
(`PrepareDepthStencilView`).

## Execution

1. Prepare DSV (create or reuse cached view via `ResourceRegistry`).
2. Set viewport & scissors (full texture if none explicitly set).
3. Clear depth (no stencil usage).
4. **Bind DSV (no RTVs) & issue geometry draw calls with multi-draw support**:
   * For each draw item: call `BindDrawIndexConstant(draw_index)` then `Draw()`
   * Shaders use `g_DrawIndex` root constant to access correct `DrawResourceIndices` entry
5. Register pass.

Draw list source: `RenderContext::opaque_draw_list` (placeholder until more
granular lists exist).

## Future Considerations

* Optional early Z statistics collection (not implemented).
* Integration with light culling pass to supply depth pyramid or clustering
  inputs.

Related: [render pass lifecycle](../render_pass_lifecycle.md), [render context &
pass registry](../render_graph.md).

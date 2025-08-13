# ShaderPass

Focus: main forward/Forward+-style color rendering using scene + (optional)
material constant buffers.

## Configuration

`ShaderPassConfig`:

* `color_texture` (optional – overrides framebuffer color attachment 0)
* `clear_color` (optional – overrides texture descriptor clear value)
* `debug_name`

Depth usage determined from `RenderContext::framebuffer` depth attachment
presence.

## Pipeline State

Rebuilt when color format or sample count changes. Depth test enabled only if a
depth attachment is present; depth writes disabled (expect depth already
populated by `DepthPrePass`). Root bindings include scene & material CBVs plus
draw index constant for multi-draw support.

## Resource Preparation

* Transition color texture → `kRenderTarget`.
* If depth present: transition depth → `kDepthRead` (read-only in this pass).

## Execution

1. Prepare RTV (and read-only DSV if depth).
2. Set viewport & scissors to full target area.
3. Clear framebuffer color (and depth if chosen to read? currently color only).
4. Bind material constants (optional).
5. **Draw geometry from `opaque_draw_list` with multi-draw support**:
   * For each draw item: call `BindDrawIndexConstant(draw_index)` then `Draw()`
   * Shaders use `g_DrawIndex` root constant to access correct `DrawResourceIndices` entry
6. Register pass.

## Future Considerations

* Split into material / lighting evaluation vs full-screen resolve.
* Add transparent / blended variant or separate pass.

Related: [DepthPrePass](depth_pre_pass.md), [render pass
lifecycle](../render_pass_lifecycle.md), [bindless
conventions](../bindless_conventions.md).

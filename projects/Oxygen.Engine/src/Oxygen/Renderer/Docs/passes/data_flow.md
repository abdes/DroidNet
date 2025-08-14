# Data Flow & Pass IO Summary

Summarises inputs/outputs of implemented and placeholder passes without
duplicating pipeline details found elsewhere.

## Implemented Passes

### DepthPrePass

* Inputs: `RenderContext::opaque_draw_list`, `scene_constants` (injected by
  renderer), depth texture.
* Outputs: populated depth texture.

### ShaderPass

* Inputs: depth texture (read-only if present), `opaque_draw_list`,
  `scene_constants` (injected by renderer), optional `material_constants`, color
  texture.
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

## Frame Setup Sequence (Phase 2+4)

1. Application prepares camera + timing → fills `SceneConstants` CPU struct
   (must happen exactly once per frame).
2. Calls `renderer->SetSceneConstants(constants)` (increments internal per-frame
   counter; last call would win but assert enforces single call).
3. (Optional) Extract current material selection →
   `renderer->SetMaterialConstants(material_constants)` (0 or 1 times per frame;
   dirty memcmp avoids redundant upload).
4. Ensure mesh resources once (or when assets change):
   `renderer->EnsureMeshResources(mesh)` – on first call per mesh, the renderer
   creates GPU buffers (if needed), registers SRVs in the bindless table, and
   caches the shader-visible indices. Neither examples nor passes should create
   vertex/index SRVs directly. Also updates the per-frame draw metadata CPU
   snapshot automatically.
5. Scene extraction (Phase 4): build draw lists from the scene using the
    current `View`.
    * Use `renderer->BuildFrame(scene, view)` to clear and repopulate
       `opaque_draw_list`, or call
       `extraction::CollectRenderItems(scene, view, renderer->OpaqueItems())`.
    * Steps: scene.Update() → traversal with VisibleFilter → build RenderItem
       per-mesh → `UpdateComputedProperties()` → CPU frustum cull → append.
6. Optionally populate/adjust any remaining lists. Do NOT set
    `scene_constants` / `material_constants` pointers directly; renderer
    injects them.
7. Invoke `renderer->ExecuteRenderGraph(...)` – during `PreExecute` the
    renderer (PreExecute):
    * Ensures / uploads per-draw structured buffers (e.g., DrawMetadata,
       WorldTransforms) if dirty.
    * Propagates their descriptor heap slots into the corresponding fields in
       `SceneConstants` (or 0xFFFFFFFF if absent).
    * Uploads SceneConstants & optional MaterialConstants if dirty.
    * Wires buffer handles into the transient `RenderContext` (cleared in
       `PostExecute`).

Notes:

* Reverse-Z is supported by `Types/View` and `Types/Frustum::FromViewProj`.
* Only opaque draw list is produced in Phase 4; transparent path is TBD.

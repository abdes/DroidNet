# Renderer Shadow System Design

Purpose: define the active renderer shadow architecture for Oxygen.

Cross-references: [lighting_overview.md](lighting_overview.md) | [passes/data_flow.md](passes/data_flow.md) | [passes/design-overview.md](passes/design-overview.md) | [implementation_plan.md](implementation_plan.md)

## Current Scope

The renderer now supports one directional shadow implementation family:

- Conventional cascaded shadow maps for directional lights.

The active renderer exposes only the conventional directional shadow path.

## Runtime Ownership

- Scene-authored shadow intent remains owned by scene/light data.
- `LightManager` gathers directional shadow candidates.
- `ShadowManager` coordinates runtime shadow work.
- `ConventionalShadowBackend` owns directional shadow planning, publication, and raster inputs.
- `ConventionalShadowRasterPass` renders the published directional cascade work.
- Forward shading resolves directional shadow visibility through shared `ShadowInstanceMetadata` and conventional directional metadata.

## Published Data

The active directional shadow path publishes:

- `ShadowInstanceMetadata`
- `DirectionalShadowMetadata`
- `ShadowFramePublication`
- `ShadowFrameBindings`
- `RasterShadowRenderPlan`

Those published buffers are routed through `ViewFrameBindings.shadow_frame_slot` and consumed by raster and lighting code.

## Design Rules

- One authored shadow model, one active runtime implementation family.
- Renderer policy decides how authored directional shadows are realized at runtime.
- Shared shadow publication must stay backend-neutral even if only one backend is currently active.
- Shadow-caster classification stays separate from main-view visibility.
- Shader-facing shadow bindings must only expose resources that are actually published for the active path.

## Validation Expectations

When changing the shadow system:

- update published shadow metadata and bindings together
- keep shader contracts aligned with C++ publication structs
- update raster planning and forward shading in the same change when contracts move
- remove obsolete docs and examples when capabilities are removed from the engine

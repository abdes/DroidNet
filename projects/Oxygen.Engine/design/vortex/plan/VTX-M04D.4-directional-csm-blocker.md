# VTX-M04D.4 Directional CSM Blocker

**Status:** `in_progress`

## Purpose

VTX-M04D.4 cannot truthfully close shadowed volumetric-light injection while
the conventional directional CSM path lacks projected-shadow proof. This plan
tracks the prerequisite parity work on the existing VTX-M03 `ShadowService`
directional baseline before volumetric fog consumes it.

## UE5.7 References

- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp`
  - `FProjectedShadowInfo::SetupWholeSceneProjection`
  - directional cascade split bounds and culling setup
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp`
  - cascade projection parameters, depth-bias scaling, and depth-bounds use
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp`
  - conventional shadow-depth pass ownership and cached split behavior
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\ForwardShadowingCommon.ush`
  - directional cascade selection and world-to-shadow projection sampling
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\ShadowFilteringCommon.ush`
  - manual PCF visibility contract and max-depth handling
- `F:\Epic Games\UE_5.7\Engine\Shaders\Private\DeferredLightPixelShaders.usf`
  - deferred direct lighting consumes a shadow attenuation/mask product before
    applying light contribution

## Oxygen Divergences To Preserve

- Oxygen uses bindless per-view `ShadowFrameBindings` rather than UE's RDG
  shadow-parameter structs.
- Oxygen currently stores directional cascades in a `Texture2DArray` instead
  of UE's packed shadow atlas.
- Oxygen world space is +Z up / -Y forward, and
  `FrameDirectionalLightSelection::direction` is the vector from a shaded point
  toward the directional-light source.
- Stage 12 deferred lighting samples the per-view shadow product directly; a
  later screen-space shadow mask may be introduced only as an explicit design
  change.

## Corrected Scope

The blocker is not only "render any depth map". The corrected directional CSM
scope is:

- propagate authored directional CSM settings into `FrameLightSelection`
  instead of carrying only cascade count
- build per-cascade split distances from authored manual distances or UE-shaped
  generated distribution within the authored max shadow distance
- publish fade/bias metadata needed by the Stage 12 receiver
- keep CPU `ShadowFrameBindings` and HLSL `DirectionalShadowCommon` in lockstep
- keep Stage 8 resource state, DSV/SRV publication, and per-view shadow slots
  valid
- verify Stage 12 produces a measurable projected-shadow attenuation in runtime
  or capture proof

## Exit Gate

This blocker remains `in_progress` until all of the following are recorded in
`IMPLEMENTATION_STATUS.md`:

- focused build/test evidence for the changed CSM code and shader ABI
- ShaderBake/catalog evidence for changed shadow or deferred-light shaders
- runtime or RenderDoc proof that Stage 8 renders directional cascade depths,
  Stage 12 consumes the conventional shadow product, and projected shadows
  affect receiver pixels

No VTX-M04D.4 shadowed-light or volumetric-fog parity claim may rely on the
current directional CSM path before this proof exists.

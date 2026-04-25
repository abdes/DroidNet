# VTX-M04D.4 Directional CSM Blocker

**Status:** `validated_for_directional_csm_projected_shadow_blocker`

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
- `F:\Epic Games\UE_5.7\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp`
  - `ComputeAccumulatedScale`, dynamic shadow distance, cascade transition
    fraction, and last-cascade fade-plane behavior
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

## Implementation Slice Evidence

The directional CSM projected-shadow blocker is now closed with implementation,
docs/status, capture evidence, debug-layer evidence, and user visual
confirmation. This does not close full VTX-M04D.4 volumetric-fog parity.

- `FrameLightSelection` now carries authored directional CSM settings:
  shadow participation, split mode, max distance, cascade distances,
  distribution exponent, transition/fade fractions, and bias terms
- `SceneRenderer` canonicalizes authored directional-light CSM settings and
  gates cascade publication on `Common().casts_shadows`
- `ShadowService` skips conventional CSM rendering when the selected
  directional light is not authored to cast shadows
- `CascadeShadowSetup` builds UE-shaped generated/manual split distances,
  per-cascade light matrices, world texel size, transition width, fade begin,
  and receiver-bias metadata
- `DirectionalShadowCommon.hlsli` consumes the published metadata for cascade
  selection, receiver bias, cascade transition blending, last-cascade fade, and
  3x3 conventional shadow sampling
- `VortexBasic` now authors its sun as shadow-casting so the runtime proof scene
  exercises the conventional directional CSM path
- Oxygen reversed-Z conventions are now carried through the conventional CSM
  path: the directional shadow surface clears to `0.0`, the shadow-depth PSO
  uses `GreaterOrEqual`, the light projection uses the reversed-Z orthographic
  helper, and shader compare/bias signs follow reversed-Z depth.
- `ShadowDepthPass` now publishes a per-cascade pass-constants CBV containing
  direct draw metadata, world-matrix, and instance-data slots. The shadow-depth
  shader no longer routes through mutable `ViewFrameBindings` for draw payloads,
  avoiding the later-frame binding hazard found in RenderDoc inspection.
- Directional CSM shader bindless guards now match Oxygen's heap conventions:
  pass constants are checked for a valid CBV handle instead of a generated
  `GLOBAL_SRV` domain, and the directional shadow texture is checked for a
  valid handle instead of the stale generated texture-domain range.
- CPU and HLSL cascade packing now match exactly. `ShadowCascadeBinding` is
  explicitly 112 bytes and carries padding matching
  `VortexShadowCascadeBinding`, preventing Stage 12 from reading mis-strided
  cascade metadata.
- `ShadowFrameBindings` now carries the selected directional light vector so
  debug-only deferred shadow-mask rendering can evaluate the same receiver
  shadow contract without depending on Stage-12 lighting bindings.
- VortexBasic's proof scene now uses a higher sun elevation and a larger floor
  receiver so the projected cube shadow lands close to the caster and remains
  easy to inspect in captures.
- The shader debug catalog now includes the deferred `directional-shadow-mask`
  debug view, and the capture probe records Stage-8 cascade writes, Stage-12
  shadow-product binding, and receiver-point lit/shadowed visibility samples.

Validation recorded in `IMPLEMENTATION_STATUS.md` covers focused build,
ShaderBake/catalog, unit tests, normal and `directional-shadow-mask` RenderDoc
captures, receiver-pixel attenuation proof, debugger-backed D3D12 debug-layer
audit, and user visual confirmation of projected shadows.

## Exit Gate

This blocker is validated because all of the following are recorded in
`IMPLEMENTATION_STATUS.md`:

- focused build/test evidence for the changed CSM code and shader ABI
- ShaderBake/catalog evidence for changed shadow or deferred-light shaders
- runtime or RenderDoc proof that Stage 8 renders directional cascade depths,
  Stage 12 consumes the conventional shadow product, and projected shadows
  affect receiver pixels
- user visual confirmation of projected shadows in the target scene

This is a prerequisite closure only. VTX-M04D.4 remains `in_progress` for
depth-aware froxel distribution, shadowed/local/sky light injection, local-fog
participating-media injection, temporal history/reprojection, and artifact
quality proof.

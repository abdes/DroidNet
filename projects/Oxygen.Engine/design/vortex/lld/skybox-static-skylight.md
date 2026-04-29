# Skybox And Static Specified-Cubemap SkyLight LLD

**Milestone:** VTX-M08
**Status:** `active_design`

This LLD defines how Vortex renders a visual cubemap skybox and consumes the
static specified-cubemap SkyLight products defined in
[`cubemap-processing.md`](cubemap-processing.md). It is not completion
evidence. VTX-M08 remains `in_progress` until full implementation and closure
proof are present.

## 1. Design Goals

VTX-M08 must make these behaviors unambiguous:

1. A scene can show a cubemap skybox as its visible background.
2. A scene can use a static specified-cubemap SkyLight as diffuse indirect
   lighting.
3. Those two behaviors are separate. Sharing the same cubemap asset is allowed,
   but enabling one must not secretly enable the other.
4. Procedural sky/atmosphere and cubemap skybox cannot both draw competing
   full-background color in the same view.
5. Directional sun lighting affects geometry and procedural atmosphere, not the
   pixels inside a static cubemap image.
6. Multi-view/offscreen/feature-gated views each get correct per-view
   publication and no cross-view leakage.

## 2. UE5.7 Source Findings That Drive This Design

### 2.1 Sky Materials Are A SceneColor-Only Sky Pass

UE routes materials flagged `IsSky` through `FSkyPassMeshProcessor`.
The SkyPass uses base-pass shaders with `bRenderSkylight=false`, and BasePass
removes every render target except SceneColor for that pass.

Relevant UE5.7 source:

- `SkyPassRendering.cpp:23-82`
- `BasePassRendering.cpp:1648-1662`
- `BasePassRendering.cpp:1728-1742`

Vortex design consequence:

- visual cubemap skybox is a background SceneColor pass, not a GBuffer/base-pass
  material contribution
- it writes no normals, material IDs, velocity, depth, or shadow data
- it must not participate in deferred lighting as geometry

Vortex already has a fullscreen Stage-15 `SkyPass` that writes SceneColor with
depth read-only and no depth writes. VTX-M08 extends this pass for cubemap and
solid-color `SkySphere` sources instead of introducing sky dome scene geometry.

### 2.2 Procedural Sky And Sky Materials Interact Explicitly

UE `SkyAtmosphereRendering` checks whether the scene has sky materials. When a
sky material exists, sky-atmosphere rendering can avoid drawing the sky pixels
as the full background and still provide aerial-perspective/atmosphere support.

Relevant UE5.7 source:

- `SkyAtmosphereRendering.cpp:1901-1908`
- `DeferredShadingRenderer.cpp:3451-3466`

Vortex design consequence:

- background selection is explicit and per view
- selected `SkySphere` cubemap/solid-color background suppresses procedural
  atmosphere as the full-background color
- atmosphere LUTs, transmittance, aerial perspective, and fog participation can
  still exist when authored and enabled

### 2.3 Sky Atmosphere Renders Before Fog

UE deferred ordering draws sky atmosphere before fog composition:

- `DeferredShadingRenderer.cpp:3451-3466`

Vortex design consequence:

- VTX-M08 skybox background must remain in the Stage-15 sky/fog family
- selected background sky renders before height/local/volumetric fog composition
  when those features apply
- fog must not inject onto far-depth pixels unless the existing environment LLD
  allows it

### 2.4 SkyLight Diffuse Uses Processed Products

UE static SkyLight diffuse does not sample the visual sky material. It consumes
the processed SkyLight irradiance product and multiplies by SkyLight color.

Relevant UE5.7 source:

- `ReflectionEnvironmentCapture.cpp:490-502`
- `ReflectionEnvironmentShared.ush:80-120`
- `BasePassPixelShader.usf:515-518`

Vortex design consequence:

- visual skybox sampling and SkyLight diffuse evaluation are separate shader
  paths
- diffuse SkyLight is an indirect-lighting term from processed products
- a scene with skybox disabled can still receive SkyLight lighting
- a scene with SkyLight disabled can still show the skybox

### 2.5 Deferred Reflections/SkyLighting Is Larger Than VTX-M08

UE's `RenderDeferredReflectionsAndSkyLighting` handles dynamic SkyLight,
reflection environment, screen-space/reflection methods, SkyLight shadowing,
and reflection capture interactions.

Relevant UE5.7 source:

- `IndirectLightRendering.cpp:1967-2024`
- `IndirectLightRendering.cpp:2143-2250`

VTX-M08 does not activate that full family. It adds only the static
specified-cubemap diffuse term through a documented LightingService boundary.
Specular reflection, reflection captures, SkyLight occlusion, and broader GI
remain future `IndirectLightingService` work.

## 3. Authored Data Contract

### 3.1 SkySphere

Existing `SkySphere` fields:

- `enabled`
- `source`: `kCubemap` or `kSolidColor`
- `cubemap_resource`
- `solid_color_rgb`
- `intensity`
- `rotation_radians`
- `tint_rgb`

Required VTX-M08 behavior:

- `kCubemap`: sample `cubemap_resource` as the visible background.
- `kSolidColor`: render `solid_color_rgb * tint_rgb * intensity` as the visible
  background.
- `rotation_radians`: yaw around Oxygen world +Z for cubemap sampling.
- Invalid or unresolved cubemap: background is unavailable; diagnostics must
  report the reason. Do not silently replace it with black unless the authored
  source is solid black.

### 3.2 SkyLight

Existing `SkyLight` fields:

- `enabled`
- `source`: `kCapturedScene` or `kSpecifiedCubemap`
- `cubemap_resource`
- `intensity_mul`
- `tint_rgb`
- `diffuse_intensity`
- `specular_intensity`
- `real_time_capture_enabled`
- `lower_hemisphere_color`
- `volumetric_scattering_intensity`
- `affect_reflections`

Required additions for VTX-M08:

- `source_cubemap_angle_radians`
- `lower_hemisphere_is_solid_color`
- `lower_hemisphere_blend_alpha`

Scene/runtime data additions land in `SkyLightEnvironmentRecord` and
`SkyLight`. Defaults must preserve VTX-M07 behavior for old scenes:

- `source_cubemap_angle_radians = 0`
- `lower_hemisphere_is_solid_color = true`
- `lower_hemisphere_color = {0, 0, 0}`
- `lower_hemisphere_blend_alpha = 1`

Cooker cubemap import remains unchanged for VTX-M08. These new fields are
scene-authoring/runtime environment state, not cubemap asset-import state. Old
scenes must load with defaults without forcing a recook solely for the new
fields.

Required behavior:

- `source == kSpecifiedCubemap`: process the cubemap according to
  `cubemap-processing.md`.
- `source == kCapturedScene`: publish unavailable; no capture fallback.
- `real_time_capture_enabled == true`: publish unavailable; no real-time
  capture fallback.
- `affect_reflections == false`: no effect in VTX-M08 because specular
  reflections are deferred; preserve the authored value for future stages.

## 4. Background Selection Policy

Per view, resolve `VisibleSkyBackground` in this order:

1. If the view feature mask disables environment, select `None`.
2. If `SkyAtmosphere` is enabled and the view opts into atmosphere rendering,
   select `SkyAtmosphere`.
3. Else if `SkySphere` is enabled and its source is valid, select `SkySphere`.
4. Else select `None`.

Reasoning:

- `SkyAtmosphere` is the procedural background owner when the view enables it.
  It must suppress visual SkySphere drawing so the UI and scene state cannot
  leave both full-frame sky paths active at once.
- `SkySphere` is explicit visual background authoring when procedural
  atmosphere is disabled for the view.
- This resolves scenes that author both systems without pass-order accidents or
  stale SkySphere payloads drawing behind the atmosphere path.

When `SkySphere` is selected:

- procedural atmosphere must not draw full-background sky color
- atmosphere LUTs may still exist for transmittance, aerial perspective, sun
  disk policy, fog coupling, or future overlay if explicitly enabled
- Stage-14/Stage-15 aerial perspective and fog still apply to scene depth where
  their feature gates allow; far-depth cubemap sky pixels are not subject to AP
  unless a later LLD explicitly adds that behavior
- height/local/volumetric fog keeps its existing feature gates and far-depth
  restrictions

When `SkyAtmosphere` is selected:

- current Stage-15 sky behavior remains the active background
- `SkySphere` fields may remain published for diagnostics but must not draw

When `None` is selected:

- Stage-15 sky pass does not draw background
- SceneColor remains whatever prior passes left for far-depth pixels
- diagnostics must distinguish "no sky selected" from "sky selected but black"

## 5. Directional Sun Interaction

Directional sun lighting has three independent roles:

1. direct lighting for opaque/translucent scene geometry
2. atmosphere-light input for procedural sky/atmosphere
3. shadow source for conventional shadow maps

Static cubemap skybox behavior:

- the cubemap pixels are authored radiance/image content
- `SkySphere::intensity` is a scene-linear radiance multiplier, not an
  exposure-compensation bypass. `1.0` is identity: it preserves imported texel
  values and does not mean the cubemap is physically calibrated or luminous
  under every exposure. Normalized HDRIs, SDR/LDR cubemaps, and artist-authored
  backgrounds commonly require authored multipliers in the hundreds or
  thousands under daylight manual exposure. Proof scenes and demo settings must
  author an intensity appropriate for the active exposure instead of adding
  hidden shader-side brightening.
- the directional sun does not illuminate, rotate, or recolor those pixels
- if the cubemap contains a visible sun, it is just part of the texture
- time-of-day cubemap synthesis and procedural sun-disk overlay are future work

Static SkyLight behavior:

- SkyLight diffuse comes from the processed specified cubemap
- directional sun direct light and SkyLight diffuse are additive lighting terms
- changing sun intensity or direction must affect direct-light debug output and
  procedural-atmosphere output, but it must not change the processed static
  SkyLight products unless the authored cubemap itself changes

Validation must include a sun-rotation/tweak check proving that geometry direct
lighting changes while the static skybox image and static SkyLight product
revision stay stable.

## 6. Render Pipeline Integration

### 6.1 Frame Planning

Extend `FramePlanBuilder::SkyState` with:

- `sky_sphere_enabled`
- `sky_sphere_source`
- `sky_sphere_cubemap_authored`
- `sky_atmo_enabled`
- `visible_sky_background`

The plan must copy the resolved background selection into each `ViewRenderPlan`
or equivalent per-view packet. The selection must be immutable for that view
once rendering starts.

### 6.2 Environment Publication

`EnvironmentLightingService::BuildEnvironmentStaticData` must publish:

- `sky_sphere.enabled = 1` only when `VisibleSkyBackground == SkySphere` and the
  source is valid
- `sky_sphere.source`
- `sky_sphere.cubemap_slot` for cubemap backgrounds
- `sky_sphere.cubemap_max_mip`
- `sky_sphere.solid_color_rgb`
- `sky_sphere.tint_rgb`
- `sky_sphere.intensity`
- `sky_sphere.rotation_radians`

For SkyLight:

- `sky_light.enabled = 1` only when diffuse static SkyLight products are valid
  and the view feature mask allows environment lighting
- the new static SkyLight diffuse SH binding points to the eight-`float4`
  coefficient buffer
- `sky_light.cubemap_slot` points to the processed cubemap for diagnostics
- `sky_light.radiance_scale` includes authored intensity and HDR product scale
- invalid specular resources remain invalid

VTX-M08 must cleanly migrate the existing environment shader ABI instead of
aliasing incompatible descriptors. Current Vortex HLSL consumers use
`irradiance_map_slot` as a TextureCube in the translucent forward path and the
local-fog path, and use `sky_light.cubemap_slot` with a visual `sky_sphere`
fallback in forward debug. The M08 ABI must therefore replace the current
`GpuSkyLightParams::pad1` / HLSL `_pad1` field with a dedicated
`diffuse_sh_slot` while preserving `sizeof(GpuSkyLightParams) == 64` and
`sizeof(EnvironmentStaticData) == 672`. Update C++ and HLSL mirrors together.
No shader may sample the visual SkySphere cubemap as diffuse SkyLight fallback,
and no shader may interpret the same descriptor field as both TextureCube and
structured SH data.

### 6.3 Stage 15 Sky Pass

Extend `VortexSkyPassPS` as a single entry point with a dynamic branch on the
published sky/background state. VTX-M08 does not require a new shader-catalog
entrypoint for skybox rendering unless implementation later changes this LLD.

Implementation behavior:

1. Load `EnvironmentStaticData` and per-view environment data.
2. If `sky_sphere.enabled != 0`:
   - if source is solid color, output color directly
   - if source is cubemap, reconstruct world view direction, apply +Z yaw
     rotation, sample TextureCube, multiply by tint and intensity
   - output scene-linear HDR radiance consistently with the current sky pass;
     Stage 22 applies the view exposure and tone mapper. The shader must not
     multiply by reciprocal exposure or auto-normalize HDR sources to make
     cubemaps look like UI images.
   - return without evaluating procedural atmosphere background
3. Else if atmosphere is selected and valid, run current sky-atmosphere path.
4. Else discard.

The sky pass keeps:

- fullscreen triangle
- SceneColor output only
- depth read-only
- no depth writes
- far-depth/depth-equal behavior matching current sky pass expectations
- per-view viewport/scissor clamping

### 6.4 Deferred Lighting Consumption

M08 expands the existing Phase-4 ambient bridge with a typed static-SkyLight SH
product. `EnvironmentLightingService` owns product generation/publication;
`LightingService` may consume the diffuse product through this documented
bridge in Stage 12. Stage 13 remains reserved for the future
`IndirectLightingService`; that future milestone must either retire or absorb
this bridge instead of creating a second diffuse SkyLight path.

Add a narrow static SkyLight diffuse hook in LightingService / deferred
lighting:

- evaluate after direct lighting inputs are available
- read GBuffer world normal and material diffuse/albedo according to existing
  Vortex deferred lighting conventions
- evaluate the SH product from the dedicated static SkyLight diffuse SH binding
- multiply by `tint_rgb`, `radiance_scale`, `diffuse_intensity`, and material
  diffuse response
- add to SceneColor as indirect diffuse

It must not:

- modify shadow maps
- affect direct-light-only debug modes
- consume the visual skybox texture as diffuse lighting
- activate reflection/specular paths
- bypass `ViewFeatureMask::kEnvironment` / no-environment gates

### 6.5 Existing Forward/Fog Consumer Migration

The ABI migration is part of M08 scope, not a follow-up cleanup. Required work:

- `ForwardMesh_PS.hlsl`: remove the current visual-sky cubemap fallback for
  SkyLight diffuse. Translucent materials receive static SkyLight diffuse in
  M08 by evaluating the same SH product as the deferred path, subject to the
  same `sky_light.enabled`, feature-mask, `radiance_scale`, tint, and
  `diffuse_intensity` gates. Validate this with a translucent object in the
  M08 proof scene.
- `ForwardDebug_PS.hlsl`: remove the current `sky_light.cubemap_slot` to
  `sky_sphere.cubemap_slot` fallback for diagnostics that represent SkyLight.
  `ibl-raw-sky` may show a selected visual skybox only when the mode is
  explicitly labelled as visual-sky sampling; SkyLight diagnostics must sample
  the processed SkyLight product and apply `radiance_scale` exactly once.
- `LocalFogVolumeCommon.hlsli`: stop treating the static SkyLight SH binding as
  a TextureCube. Volumetric/local-fog SkyLight injection may remain deferred for
  M08, but it must be explicitly gated and must not sample the visual skybox or
  a mismatched descriptor.
- Existing forward specular branches guarded by `prefilter_map_slot` and
  `brdf_lut_slot` stay in place. M08 publishes those slots as
  `K_INVALID_BINDLESS_INDEX` unless real specular products are implemented, and
  it must not delete future specular code paths as part of the diffuse SH work.
- `EnvironmentStaticData` C++ and HLSL contracts: update together, then run
  shader catalog/ABI validation. M08 must not leave dual meanings for
  `irradiance_map_slot`.

### 6.6 Volumetric/Fog Interaction

VTX-M08 does not redesign fog. Required preservation:

- current atmosphere/fog/aerial-perspective behavior from M04D remains intact
- selected skybox background renders before fog composition in Stage 15
- far-depth sky pixels are still protected by the existing fog contracts
- volumetric SkyLight injection remains governed by volumetric-fog product
  validity; it must not be faked from the visual skybox

## 7. Multi-View And Offscreen Rules

Static products may be shared by source key across views, but all consumption
decisions are per view:

- main, auxiliary, and offscreen views each publish their own environment static
  slot
- a no-environment view must have `sky_sphere.enabled == 0` and
  `sky_light.enabled == 0`
- a diagnostics-only view may sample products only through diagnostics mode
- auxiliary views must not inherit the primary view's selected background
- product generation happens once per source key, not once per view

RenderDoc proof must include at least one multi-view or offscreen case where one
view uses the cubemap sky/SkyLight and another disables environment.

## 8. Diagnostics

Update `DiagnosticsService` / shader debug registry behavior:

- `ibl-raw-sky`: valid when a processed cubemap or selected visual cubemap SRV
  exists; shows view-direction sampling. When the source is a processed
  SkyLight cubemap, apply `radiance_scale` exactly once.
- `ibl-irradiance`: valid only when diffuse SH product exists; shows
  normal-based SH result
- `ibl-only`: valid only when diffuse SH product exists; output excludes direct
  lighting
- `direct-plus-ibl`: valid when direct lighting path and diffuse SH product are
  valid
- specular debug modes remain unavailable unless specular products are actually
  implemented

Unsupported diagnostics must show a clear unavailable reason in the capture
manifest and UI/reporting path. They must not silently render black.

## 9. Implementation Slices

### Slice A: Data And Plan State

- Add SkyLight angle and lower-hemisphere boolean to scene/runtime/data records.
- Preserve backwards-compatible defaults.
- Extend FramePlanBuilder sky background selection.
- Add unit tests for selection priority, feature-mask gates, and
  `diffuse_intensity == 0` disabling SkyLight diffuse shading while keeping
  diagnostics eligible to inspect generated products.

### Slice B: Cubemap Product Processing

- Implement static specified-cubemap product key/state.
- Resolve TextureCube resources through existing Vortex binders.
- Implement lower-hemisphere and rotation processing.
- Compute average brightness and diffuse SH.
- Publish explicit valid/unavailable/stale states.

### Slice C: Skybox Rendering

- Publish `GpuSkySphereParams`.
- Extend `VortexSkyPassPS` for solid-color and cubemap skybox.
- Add proof scene with both procedural sky and cubemap skybox authored.
- Validate selected background is deterministic.

### Slice D: Diffuse SkyLight Consumption

- Migrate the environment shader ABI to a dedicated static SkyLight SH binding.
- Update existing translucent and local-fog consumers: translucent consumes the
  new SH binding correctly; local-fog publishes an explicit unavailable/deferred
  state if it does not consume SH in M08.
- Update `ForwardDebug_PS.hlsl` diagnostics so SkyLight debug modes do not fall
  back to the visual skybox cubemap.
- Remove/gate the current visual-sky cubemap fallback for SkyLight diffuse.
- Add deferred static SkyLight diffuse term.
- Add IBL-only and direct-plus-IBL debug output.
- Validate SkyLight-only, skybox-only, direct-only, combined views, and any
  translucent/fog consumer that remains enabled.

### Slice E: Proof And Closeout

- Add CDB/debug-layer proof wrapper.
- Add RenderDoc analyzer.
- Add 60-frame allocation-churn proof.
- Repeat allocation-churn proof after toggling SkyLight off and on.
- Update PRD/architecture/design/plan/status with actual closure evidence only
  after validation passes.

## 10. Validation Gate

VTX-M08 cannot be marked validated until all of these pass:

- focused unit tests for scene data, selection policy, product state, SH
  evaluation, and feature gates
- focused build of Vortex and affected examples/tools
- ShaderBake/catalog validation if shader contracts or shader files changed
- CDB/D3D12 debug-layer runtime proof
- RenderDoc scripted analysis proving skybox draw, product publication, and
  diffuse SkyLight contribution
- allocation-churn proof over at least 60 steady-state frames
- visual validation scene approved by the user
- `git diff --check`

## 11. Required Residual-Gap Record

Closure must explicitly state whether these remain deferred:

- captured-scene SkyLight
- real-time capture
- cubemap blending
- SkyLight AO/shadowing
- static/baked lightmap SkyLight integration
- specular reflection contribution
- broader reflection probes
- procedural sun-disk overlay on static cubemap skybox

No closure report may imply those are implemented unless code and proof exist.

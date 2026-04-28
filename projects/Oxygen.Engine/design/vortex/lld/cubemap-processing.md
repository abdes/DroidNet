# Cubemap Processing LLD

**Milestone:** VTX-M08
**Status:** `active_design`

This LLD defines the VTX-M08 static specified-cubemap processing path used by
diffuse SkyLight lighting. Visual skyboxes may share the same authored cubemap
asset and the same resource-resolution path, but they do not share the
renderer-owned SkyLight lighting products. This is implementation guidance, not
closure evidence. VTX-M08 remains `in_progress` until every implementation
slice is implemented, tested, and runtime proof is recorded.

## 1. Scope

VTX-M08 implements a production-clean static cubemap baseline:

- resolve scene-authored cubemap assets for `SkySphere` and `SkyLight`
- render the source cubemap as a visual skybox when selected by the sky policy
- process a static specified-cubemap SkyLight into renderer-owned lighting
  products
- publish explicit valid/unavailable/stale states to shaders and diagnostics
- consume the diffuse SkyLight product in the Vortex deferred lighting path

The milestone does not implement captured-scene SkyLight, real-time capture,
cubemap blending, SkyLight occlusion, baked/static-lightmap integration,
reflection probes, or full specular reflection contribution.

## 2. UE5.7 Source Findings That Drive This Design

The following UE5.7 mechanisms are requirements for Vortex behavior. They are
recorded as concrete design inputs, not as a blanket "do what UE does" rule.

### 2.1 Source Modes And Authored State

UE `USkyLightComponent` has two source modes:

- `SLS_CapturedScene`: capture distant scene/sky into a cubemap
- `SLS_SpecifiedCubemap`: use an authored `UTextureCube`

Relevant UE5.7 source:

- `Engine/Classes/Components/SkyLightComponent.h:87-89`
- `Engine/Classes/Components/SkyLightComponent.h:114-124`

VTX-M08 implements only the specified-cubemap branch. Captured scene and
real-time capture remain unavailable states with explicit diagnostics.

### 2.2 Specified Cubemap Copy And Rotation

UE copies the specified cubemap into a renderer-owned scratch/processed cubemap,
applying `SourceCubemapAngle` as a yaw rotation and optional lower-hemisphere
replacement during the copy:

- `ReflectionEnvironmentCapture.cpp:1780-1808`
- `ReflectionEnvironmentCapture.cpp:681-686`

Vortex must not sample the authored asset directly as the final SkyLight
lighting product. It must create renderer-owned products with a cache key that
includes the source cubemap, source revision, output resolution, rotation,
lower-hemisphere policy, and processing settings.

### 2.3 FP32 HDR Range Preservation

UE stores the processed runtime cubemap in FP16 for performance. When the input
specified cubemap is FP32 and exceeds FP16 range, UE first computes max
luminance, scales the copied FP16 texture down, and stores a
`SpecifiedCubemapColorScale` so lighting can be expanded back to the original
range:

- `ReflectionEnvironmentCapture.cpp:1766-1768`
- `ReflectionEnvironmentCapture.cpp:1794-1815`

Vortex must preserve HDR intent. The VTX-M08 product contract therefore carries
both the processed FP16 product and a `source_radiance_scale` multiplier. If
source content fits FP16, the scale is `1.0`. If source content is FP32 or
otherwise exceeds the selected storage range, the processor records the inverse
scale required for lighting evaluation.

### 2.4 Filtered Products And Diffuse Irradiance

UE processing runs:

1. premultiply mip-zero alpha
2. generate cube mips
3. compute diffuse irradiance SH when requested
4. filter cubemap for reflection consumers

Relevant UE5.7 source:

- `ReflectionEnvironmentCapture.cpp:490-502`

VTX-M08 must implement the diffuse product. The specular/prefilter product may
be generated only if needed for diagnostics or future-proofing, but it is not a
VTX-M08 lighting success criterion.

### 2.5 Diffuse SH Packing And Evaluation

UE evaluates diffuse sky lighting from preconvolved SH, not from a per-pixel
TextureCube sample. `ReflectionEnvironmentShared.ush` stores the diffuse
convolution weights in the uploaded coefficients and evaluates them with the
normal. Base pass multiplies the lookup by `ResolvedView.SkyLightColor.rgb`.

Relevant UE5.7 source:

- `ReflectionEnvironmentShared.ush:80-120`
- `BasePassPixelShader.usf:515-518`
- `ReflectionEnvironment.cpp:621-707`

Vortex must publish diffuse irradiance in a shader-friendly coefficient buffer
and evaluate it per shaded pixel using the world-space normal. This is required
for stable diffuse IBL and avoids an expensive per-pixel cubemap convolution.

### 2.6 Average Brightness

UE computes and uploads average brightness alongside SH data. The shader uses it
for reflection brightness paths:

- `ReflectionEnvironmentCapture.cpp:351-379`
- `ReflectionEnvironment.cpp:706-707`
- `ReflectionEnvironmentShared.ush:10-13`

VTX-M08 must compute and publish average luminance/brightness as metadata even
though full specular reflection is deferred. Diagnostics and future reflection
paths must not have to reinterpret the source cubemap to recover this value.

### 2.7 Static-Lighting And Real-Time Capture Gates

UE does not upload static-light diffuse SkyLight through the dynamic deferred
path because that contribution is baked into lightmaps, and real-time capture
has a separate GPU path:

- `ReflectionEnvironment.cpp:673-678`
- `IndirectLightRendering.cpp:1996-2001`
- `SkyLightComponent.cpp:1145-1148`

Oxygen does not have baked SkyLight lightmaps in VTX-M08. Therefore:

- `SkyLight::kSpecifiedCubemap` with `affect_global_illumination=true` can feed
  VTX-M08 dynamic diffuse lighting.
- `real_time_capture_enabled=true` remains authored but unavailable.
- Future baked/static-lighting integration must add a separate gate before it
  can disable dynamic diffuse SkyLight.

## 3. Current Oxygen Baseline

Existing systems to reuse:

- `TextureType::kTextureCube` and `kTextureCubeArray`
- Cooker import intents `kHdrEnvironment` and `kHdrLightProbe`
- equirectangular-to-cube and layout extraction in
  `src/Oxygen/Cooker/Import`
- D3D12 TextureCube SRV support in `Graphics/Direct3D12/Texture.cpp`
- scene records `SkyLightEnvironmentRecord` and `SkySphereEnvironmentRecord`
- runtime scene systems `SkyLight` and `SkySphere`
- `EnvironmentViewProducts::sky_light`
- `EnvironmentStaticData::sky_light` and `EnvironmentStaticData::sky_sphere`
- placeholder `IblProcessor` / `IblProbePass` invalid-state plumbing
- IBL debug-mode vocabulary in `ShaderDebugMode`

The implementation must extend these systems. It must not create a parallel
environment asset loader, a separate legacy renderer path, or demo-only
lighting shortcuts.

## 4. Product Model

### 4.1 Runtime Product Types

Add a renderer-owned static cubemap product state under
`src/Oxygen/Vortex/Environment/Types`:

```cpp
struct StaticSkyLightProductKey {
  content::ResourceKey source_cubemap;
  std::uint64_t source_revision;
  std::uint32_t output_face_size;
  std::uint32_t source_format_class;
  float source_rotation_radians;
  bool lower_hemisphere_solid_color;
  glm::vec3 lower_hemisphere_color;
  float lower_hemisphere_blend_alpha;
};

enum class StaticSkyLightProductStatus {
  kDisabled,
  kUnavailable,
  kRegeneratingCurrentKey,
  kValidCurrentKey,
  kStaleWrongKeyRejected,
};

enum class StaticSkyLightUnavailableReason {
  kNone,
  kCapturedSceneDeferred,
  kRealTimeCaptureDeferred,
  kMissingCubemap,
  kResourceResolveFailed,
  kNotTextureCube,
  kUnsupportedFormat,
  kProcessingFailed,
  kShaderConsumerMigrationIncomplete,
};

struct StaticSkyLightProducts {
  StaticSkyLightProductKey key;
  ShaderVisibleIndex source_cubemap_srv;
  ShaderVisibleIndex processed_cubemap_srv;
  ShaderVisibleIndex diffuse_irradiance_sh_srv;
  ShaderVisibleIndex prefiltered_cubemap_srv;
  ShaderVisibleIndex brdf_lut_srv;
  std::uint32_t processed_cubemap_max_mip;
  std::uint32_t prefiltered_cubemap_max_mip;
  std::uint32_t product_revision;
  float source_radiance_scale;
  float average_brightness;
  StaticSkyLightProductStatus status;
  StaticSkyLightUnavailableReason unavailable_reason;
};
```

Implementation may rename fields to match local style, but it must preserve
these concepts.

### 4.2 Diffuse Coefficient Layout

VTX-M08 must publish a structured buffer with eight `float4` elements:

- elements `0..6`: packed three-band diffuse SH coefficients with UE-style
  diffuse convolution coefficients baked into the values
- element `7`: `{ average_brightness, average_brightness,
  average_brightness, average_brightness }`

The shader evaluator must match the UE `GetSkySHDiffuse` shape:

```hlsl
float3 EvaluateStaticSkyDiffuse(float3 normal_ws)
{
    float4 n = float4(normal_ws, 1.0f);
    float3 a = float3(dot(sh[0], n), dot(sh[1], n), dot(sh[2], n));
    float4 bvec = n.xyzz * n.yzzx;
    float3 b = float3(dot(sh[3], bvec), dot(sh[4], bvec), dot(sh[5], bvec));
    float c = n.x * n.x - n.y * n.y;
    return max(0.0f.xxx, a + b + sh[6].xyz * c);
}
```

This layout is deliberately close to UE because it carries the optimization:
diffuse convolution is precomputed once, and pixels pay a small fixed SH
evaluation cost.

### 4.3 Environment Probe State

Extend `EnvironmentProbeState` / `EnvironmentProbeBindings` so static
specified-cubemap products can become the usable resource set:

- `environment_map_srv`: processed source cubemap
- `diffuse_sh_srv`: SH coefficient structured buffer SRV
- `irradiance_map_srv`: preserved as a TextureCube-compatible slot until all
  existing shader consumers are explicitly migrated or removed
- `prefiltered_map_srv`: valid only if VTX-M08 generates it; otherwise invalid
- `brdf_lut_srv`: invalid unless a BRDF LUT exists
- `probe_revision`: increments when products are regenerated

`ProbeStateHasUsableResources` must gain a diffuse-only concept for VTX-M08.
Today it requires environment, irradiance, prefiltered, and BRDF resources. For
VTX-M08 diffuse lighting, a product is usable when:

- processed/source cubemap SRV is valid for diagnostics and skybox sharing
- diffuse SH SRV is valid
- `product_revision != 0`
- source/cook resolution and source key match the current authored model

Specular resources remain invalid and must not block diffuse SkyLight.

## 5. Processing Pipeline

### 5.1 Input Validation

Given a `SkyLightEnvironmentModel`:

1. If `enabled == false`, publish disabled products.
2. If `source == kCapturedScene`, publish `authored_enabled` plus
   `ibl_unavailable` with reason `captured_scene_deferred`.
3. If `real_time_capture_enabled == true`, publish unavailable with reason
   `real_time_capture_deferred`.
4. If `source == kSpecifiedCubemap` and `cubemap_resource` is empty, publish
   unavailable with reason `missing_cubemap`.
5. Resolve the cubemap through the Vortex resource binder/content system.
6. Validate that the resolved resource is `TextureType::kTextureCube`,
   `array_layers == 6`, and has an HDR-capable linear format.
7. Reject non-cube resources with an unavailable state. Do not bind an error
   texture as a valid SkyLight product.

For `SkySphere`, the visual path may use the same source cubemap SRV, but
visual validity is separate from SkyLight product validity.

### 5.2 Resolution Policy

The processed cubemap face size must be a power of two.

VTX-M08 default:

- use authored/cooked face size when it is power-of-two and at or below the
  renderer limit
- otherwise clamp to the nearest lower power-of-two not exceeding the renderer
  limit
- expose a future config point equivalent to UE `CubemapResolution`, but do not
  add broad user-facing UI until implementation needs it

The implementation must record the selected face size in diagnostics.

### 5.3 Lower-Hemisphere Policy

Current Oxygen `SkyLight` stores `lower_hemisphere_color` but not a separate
`lower_hemisphere_is_solid_color` bool or blend strength. VTX-M08 must add
explicit state instead of inferring behavior from color.

Required default:

- `lower_hemisphere_is_solid_color = true`
- `lower_hemisphere_color = {0, 0, 0}`
- `lower_hemisphere_blend_alpha = 1.0`

Reason: UE's default lower hemisphere behavior is black/solid, which prevents
light from below the planet/ground from incorrectly illuminating outdoor scenes.
This also matches Oxygen's +Z-up world and planet-ground expectations.

During cubemap copy or irradiance integration, directions with negative
world-up component after rotation blend toward `lower_hemisphere_color` when
the flag is enabled. `lower_hemisphere_blend_alpha = 1.0` is a solid
replacement. `0.0` leaves the sampled cubemap unchanged. If the flag is
disabled, sample the source cubemap normally.

### 5.4 Rotation Convention

Use `SkyLight::source_cubemap_angle` for lighting products and
`SkySphere::rotation_radians` for visual skybox products. VTX-M08 must add
`source_cubemap_angle_radians` to scene/runtime `SkyLight` state; it must not
reuse the SkySphere visual rotation implicitly.

Rotation axis:

- yaw around Oxygen world +Z
- positive angle follows the existing engine right-handed yaw convention
- the shader/compute helper must be shared by skybox sampling and SkyLight
  processing to avoid visual/lighting drift when both use the same cubemap

### 5.5 Product Generation Steps

The first implementation may run as compute or graphics passes, but the product
sequence is fixed:

1. Resolve the source TextureCube SRV.
2. Copy/resample the source into a renderer-owned processed cubemap:
   - apply yaw rotation
   - apply lower-hemisphere replacement
   - apply FP32-to-FP16 scale if needed
   - store `source_radiance_scale`
3. Generate mips for the processed cubemap.
4. Compute average brightness/luminance from the processed cubemap in the
   processed FP16-domain.
5. Compute diffuse irradiance SH from the processed cubemap in the processed
   FP16-domain.
6. Upload/publish the eight-`float4` SH buffer.
7. Publish a new product revision atomically after every required resource is
   valid.

`source_radiance_scale` is applied exactly once at shading/diagnostic
evaluation time through `radiance_scale`. It must not be multiplied into the SH
buffer or the stored average brightness. This matches UE's split between a
scaled runtime cubemap and the separate specified-cubemap color scale, and it
prevents accidental double expansion of FP32 HDR sources.

If a generation step fails, keep the previous valid product only when the
current source key matches the previous key. If the source key changed, publish
unavailable instead of silently using stale lighting from another cubemap.

### 5.6 Cache And Lifetime

`EnvironmentLightingService` owns the current static SkyLight products. The
cache key includes every field in `StaticSkyLightProductKey`. A key change
marks products stale and schedules regeneration. A frame may publish stale
products only if:

- the source key did not change
- only non-product-affecting view state changed
- diagnostics report `kValidCurrentKey` for shader-visible consumers

For M08, product generation can complete synchronously in the frame setup path
if the implementation keeps D3D12 debug-layer clean and records no steady-state
allocation churn after warmup. A later async processor may be introduced only
if it preserves identical product state and invalidation semantics.

## 6. Shader Contracts

### 6.1 Environment Static Data

Current `GpuSkyLightParams` already contains:

- `tint_rgb`
- `radiance_scale`
- `diffuse_intensity`
- `specular_intensity`
- `source`
- `enabled`
- `cubemap_slot`
- `brdf_lut_slot`
- `irradiance_map_slot`
- `prefilter_map_slot`
- mip counts and generation

For VTX-M08:

- `enabled == 1` means diffuse SkyLight products are valid and may be sampled.
- replace the current C++ `GpuSkyLightParams::pad1` / HLSL `_pad1` field with
  `diffuse_sh_slot` for the eight-`float4` SH structured buffer.
- keep `sizeof(GpuSkyLightParams) == 64` and
  `sizeof(EnvironmentStaticData) == 672` unless implementation finds a
  documented need to grow the ABI; if it grows, the LLD/status must be patched
  before code lands.
- update the C++ and HLSL mirrors together and run shader catalog/ABI
  validation in the same slice.
- do not repurpose `irradiance_map_slot` as the SH buffer.
- `cubemap_slot` points to the processed cubemap for diagnostics and future
  reflection consumers.
- `irradiance_map_slot`, if retained, remains TextureCube-compatible and must
  be invalid in M08 unless a real cubemap irradiance product is generated.
- `prefilter_map_slot` and `brdf_lut_slot` may remain invalid.
- `radiance_scale` contains `intensity_mul * source_radiance_scale`.
- `diffuse_intensity` gates diffuse SkyLight only.
- `specular_intensity` remains published but must not activate specular
  reflections in M08.

Current Vortex shaders already consume SkyLight/SkySphere cubemap fields in the
translucent forward, forward-debug, and local-fog paths. VTX-M08 must audit and
patch every shader under `Shaders/Vortex` that references
`sky_light.cubemap_slot`, `sky_light.irradiance_map_slot`,
`sky_light.prefilter_map_slot`, `sky_light.brdf_lut_slot`, or
`sky_sphere.cubemap_slot` before setting `sky_light.enabled = 1`. A shader that
expects a TextureCube must never receive the SH structured-buffer slot. Until a
consumer is migrated, it must see an invalid/unavailable resource and an
explicit reason rather than sampling the visual skybox or a mismatched
descriptor.

### 6.2 SH Packing Constants

The processor uploads the same packed SH shape used by UE's dynamic SkyLight
buffer. The coefficients below are derived from local UE5.7
`SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance`:

```text
SqrtPI = sqrt(pi)
Coefficient0 = 1 / (2 * SqrtPI)
Coefficient1 = sqrt(3) / (3 * SqrtPI)
Coefficient2 = sqrt(15) / (8 * SqrtPI)
Coefficient3 = sqrt(5) / (16 * SqrtPI)
Coefficient4 = 0.5 * Coefficient2
```

Packed elements:

```text
sh[0] = {-C1*R3, -C1*R1,  C1*R2, C0*R0 - C3*R6}
sh[1] = {-C1*G3, -C1*G1,  C1*G2, C0*G0 - C3*G6}
sh[2] = {-C1*B3, -C1*B1,  C1*B2, C0*B0 - C3*B6}
sh[3] = { C2*R4, -C2*R5, 3*C3*R6, -C2*R7}
sh[4] = { C2*G4, -C2*G5, 3*C3*G6, -C2*G7}
sh[5] = { C2*B4, -C2*B5, 3*C3*B6, -C2*B7}
sh[6] = { C4*R8,  C4*G8, C4*B8, 1}
sh[7] = { average_brightness, average_brightness,
          average_brightness, average_brightness }
```

The SH integration samples directions using the same TextureCube face
orientation as Vortex's D3D12 TextureCube SRV and Cooker cubemap assembly. Each
texel is weighted by its cube-map solid angle and the accumulated solid angle is
used for normalization. Lower-hemisphere testing happens in Oxygen world space
after source rotation, with +Z as up.

### 6.3 Diffuse Lighting Formula

Deferred diffuse lighting contribution:

```hlsl
float3 irradiance = EvaluateStaticSkyDiffuse(diffuse_sh_slot, world_normal);
float3 sky_diffuse = irradiance
  * env.sky_light.tint_rgb
  * env.sky_light.radiance_scale
  * env.sky_light.diffuse_intensity;
```

The contribution is added as indirect diffuse lighting, not as direct light.
It must not cast shadows, affect direct-light debug views, or change shadow
maps.

### 6.4 Debug Modes

Existing IBL debug names become actionable:

- `ibl-raw-sky`: show processed cubemap sample by view direction
- `ibl-irradiance`: show diffuse SH evaluated by world normal
- `ibl-only`: show only static SkyLight diffuse
- `direct-plus-ibl`: show direct lighting plus static SkyLight diffuse

Diagnostic consumers of `cubemap_slot` must apply `radiance_scale` exactly once
to recover physical luminance from the FP16-stored processed cubemap. SH and
`average_brightness` exclude `source_radiance_scale`; they are scaled only at
evaluation by `radiance_scale`.

Specular debug modes remain unavailable unless VTX-M08 implements the
prefilter/BRDF product. Unavailable modes must report a reason, not black output
that looks like a rendering failure.

## 7. Feature Gates And Variants

Required behavior:

- `no-environment`: no skybox draw, no static SkyLight products, invalid
  environment product flags.
- `no-volumetrics`: no effect on static SkyLight diffuse; volumetric SkyLight
  injection remains governed by the volumetric fog path.
- `diagnostics-only`: product buffers may publish diagnostics, but scene
  lighting consumers must ignore them unless the active diagnostic mode
  explicitly samples them.
- multi-view: products are shared per source key, but view publication remains
  per view. A view with no-environment must not consume another view's SkyLight.
- offscreen: offscreen scene views consume products exactly like main views
  when their feature mask allows environment lighting.
- reflection/capture views: full reflection capture remains deferred; static
  cubemap products can be visible to diagnostics only.

## 8. Tests And Proof

### 8.1 Unit Tests

Add focused tests for:

- source-mode classification: captured/realtime/spec-cubemap/missing cubemap
- cache key changes for cubemap, revision, rotation, lower hemisphere,
  resolution
- invalid-state publication without accidental valid SRVs
- diffuse-only product usability without specular resources
- lower-hemisphere solid-color behavior for +Z-up directions
- FP32 HDR scale metadata when max luminance exceeds FP16 range
- FP32 source with `max_luminance` above the FP16 storage range: SH-evaluated
  white-Lambert luminance and `cubemap_slot`-sampled luminance match the
  original source within tolerance after exactly one `radiance_scale`
  application
- SH packing/evaluation against a known synthetic cubemap
- shader-consumer migration: no shader still samples an SH structured-buffer
  slot as a TextureCube, and no lighting path falls back to the visual skybox
  cubemap for diffuse SkyLight. The audit must enumerate all shader files that
  reference `sky_light.*_slot` or `sky_sphere.cubemap_slot`, including
  `ForwardMesh_PS.hlsl`, `ForwardDebug_PS.hlsl`, and
  `LocalFogVolumeCommon.hlsli`.

### 8.2 Runtime Proof Scene

Add a VTX-M08 validation scene with:

- a high-contrast cubemap where +Z and -Z hemispheres have distinct colors
- a grey Lambert sphere, white rough floor, and normal-varied test mesh
- no direct light variant, proving SkyLight diffuse alone
- directional sun variant, proving direct light and SkyLight are additive and
  separate
- lower-hemisphere solid-color toggle, proving underside/ground response
- skybox-only variant, proving visible sky does not imply SkyLight lighting
- SkyLight-only variant, proving lighting does not imply visible skybox

### 8.3 RenderDoc / Scripted Analysis

The proof script must assert:

- source TextureCube SRV is bound for visual skybox when expected
- static SkyLight processing pass creates or publishes the diffuse SH buffer
- `EnvironmentStaticData.sky_light.enabled == 1` only when diffuse products are
  valid
- `diffuse_sh_slot` is valid in SkyLight-enabled views
- `irradiance_map_slot` is not rebound to SH while TextureCube consumers remain
- `prefilter_map_slot` and `brdf_lut_slot` are invalid unless implemented
- `ibl-only` output is non-black on upward-facing and side-facing surfaces
- direct-light-only and IBL-only modes differ as expected
- no D3D12 debug-layer errors

### 8.4 Allocation Churn

Run at least 60 steady-state frames after product generation. There must be no
per-frame resource allocation churn for already-generated static products.
Repeat the proof after toggling SkyLight off and back on so disposal,
republication, and product reuse do not hide churn.

## 9. Residual Gaps To Record At Closure

The VTX-M08 closure report must explicitly list any of these that remain
deferred:

- captured-scene SkyLight
- real-time capture
- cubemap blending / time-of-day transition
- SkyLight AO / DFAO / bent-normal occlusion
- baked/static-lighting SkyLight integration
- specular reflection contribution
- reflection captures and probe arrays
- volumetric-cloud sky capture

These are not blockers for VTX-M08 if the diffuse static cubemap baseline is
implemented and proven, but they must remain visible.

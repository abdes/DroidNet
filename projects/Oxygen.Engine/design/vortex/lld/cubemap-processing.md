# Cubemap processing

Specified cubemaps and captured sky feed the same complete IBL product set:
processed radiance, diffuse SH, GGX prefilter, BRDF lookup and generation metadata.
Environment owns processing and publication. The visible skybox continues to
sample its authored asset independently.

Read: [source adapter](#source-adapter), [processing](#processing),
[SH packing](#diffuse-sh-packing), [remaining products](#remaining-products).
[Captured-sky IBL](captured-sky-ibl.md) owns HDR precision, GGX/BRDF algorithms,
scheduling and lifetime. [VX-IBL-01](../milestones/VX-IBL-01/README.md) owns delivery;
[VTX-M08](../milestones/VTX-M08/README.md) retains the original static baseline proof.

## Source adapter

`IblProcessor` resolves specified sources through the existing resource binder
and content system. A valid source is a resident six-face TextureCube with a
supported linear floating-point format. Missing, unresolved, incompatible or
failed sources report unavailable; the visual skybox and error textures are not
fallback lighting sources. Pending uploads retry through the existing owners.

The processing identity includes resource key/revision, output face size/format,
+Z yaw and lower-hemisphere policy. The output size is the largest power of two
not exceeding the source width. Captured sky uses the common 128-face policy.
Intensity, tint, contribution multipliers and exposure do not change radiance
products. Source replacement is structural and uses immediate processing.

Lower-hemisphere replacement is evaluated in Oxygen world space (+Z up). The
explicit boolean enables blending toward `lower_hemisphere_color` below the
horizon: alpha 1 replaces fully; alpha 0 preserves the source. Defaults are
solid black/alpha 1. SkyLight yaw and SkySphere yaw are separate authored values
using the same right-handed +Z convention.

The cooker already supports HDR environment/probe import and equirectangular or
layout-to-cube conversion. Processing reuses those assets, D3D12 TextureCube SRVs
and the existing upload/submission machinery.

## Processing

The GPU path copies/resamples the source, applies rotation/hemisphere policy,
reduces range and validity, generates source mips, integrates SH and builds the
GGX prefilter. The shared BRDF lookup is initialized once. Publication exposes
the complete generation atomically, after producers are submitted in order.
Consumers never accept diffuse-only readiness or incomplete faces/mips.

Qualified specular sampling normally uses FP16; processed-cube consumers retain
canonical FP32 sampling. Range scaling and texel guards preserve HDR
and small radiance that becomes visible after intensity changes; FP32 remains
the automatic fallback. The exact storage/native-filtering limits, mip mapping,
sample counts and metadata ABI live in
[products and processing](captured-sky-ibl.md#3-products-and-processing).

SH and average brightness are stored in the processing scale. Evaluation applies
`source_radiance_scale` exactly once, together with authored intensity. Brightness
metadata is available without re-reading the source. Diffuse evaluates SH with
the world normal; specular uses the matching prefilter/BRDF lookup. Neither
changes direct lighting or shadow maps.

CPU `StaticSkyLightProcessor` remains an independent reference/helper; canonical
renderer publication uses GPU products. Retained CPU code does not provide a
second runtime lighting path.

## Diffuse SH packing

### Coefficient layout

The processor publishes a structured buffer with eight `float4` elements:

- elements `0..6`: packed three-band diffuse SH coefficients with UE-style
  diffuse convolution coefficients baked into the values
- element `7`: `{ average_brightness, average_brightness,
average_brightness, average_brightness }`

The shader evaluator matches the UE `GetSkySHDiffuse` shape:

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

### Packing constants

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

## Publication and lifetime

`EnvironmentFrameBindings` carries the BRDF lookup and complete IBL validity;
`GpuSkyLightParams` carries typed radiance/SH/prefilter/metadata slots and authored
multipliers. Invalid metadata produces zero lighting. The source contract owns
exact buffer sizes and flag meanings; C++ and HLSL are changed together.

Immediate authoring consumes the current snapshot. Runtime changes can keep a
complete older generation while a frozen candidate advances; its age and final
convergence remain bounded. Failed or structurally replaced sources use the
common [failure rules](captured-sky-ibl.md#4-readiness-invalidation-and-lifetime).

Products are per scene, shared by main/auxiliary/offscreen views. Nexus reuse and
retirement tickets plus Graphics submission lifetimes protect queued readers
and retained captures. The pool is bounded and unchanged sources allocate or
convolve nothing after warmup.

## References and validation

Local UE5.7 references that informed the retained conventions:

- `ReflectionEnvironmentCapture.cpp:490–502,681–686,1780–1815`: copy, source yaw,
  hemisphere replacement, HDR scale and product ordering.
- `ReflectionEnvironmentShared.ush:80–120` and `ReflectionEnvironment.cpp:621–707`:
  packed diffuse SH and brightness.
- `SkyLightComponent.h:87–124`: captured versus specified source authority.

The [current evidence](../milestones/VX-IBL-01/validation.md) covers constant and
directional cubes, labelled faces, horizon/seams, HDR range, independent GGX and
BRDF references, native filtering, material images, source changes and retirement.
Original VTX-M08 evidence remains at its recorded static-diffuse scope.

## Remaining products

Cubemap blending, SkyLight AO/bent normals, baked integration, probe arrays and
cloud capture remain [VX-SKY-01](../OPEN_ITEMS.md#p3--unscheduled-capabilities).
They build on the same source validation, processing identity and lifetime.

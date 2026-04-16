# Deferred Lighting LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.6
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

The deferred direct-lighting pass at stage 12 — fullscreen pass-per-light
for directional lights and one-pass bounded-volume lighting for point/spot
lights. All light types read GBuffer products, evaluate Cook-Torrance BRDF,
and accumulate into SceneColor.

In Phase 3 the deferred lighting logic lives inline in `SceneRenderer` as
a file-separated method. In Phase 4A it migrates into `LightingService`.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 10 (RebuildSceneTextures) — GBuffers now SRV-readable | |
| Predecessors (reserved) | Stage 11 (MatComposite post — stub) | |
| **This** | **Stage 12 — Deferred Direct Lighting** | |
| Successor | Stage 13 (IndirectLighting — reserved) | |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage table, row 12
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [DESIGN.md §7](../DESIGN.md) — deferred lighting design
- UE5 reference: `RenderLights` / `RenderDeferredLighting` families

### 1.4 Required Invariants For This Module

This module must preserve the following invariants from
[ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md):

- in Phase 3, stage 12 is temporary inline `SceneRenderer` orchestration
- the deferred-light shader family still belongs to the Lighting domain and is
  authored under that file home even before CPU-side ownership migrates
- scene-texture access flows through published `ViewFrameBindings` →
  `SceneTextureBindings`; no local binding synthesis is allowed

## 2. Interface Contracts

### 2.1 Phase 3 Location (Inline)

In Phase 3, deferred lighting is a file-separated method of `SceneRenderer`:

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    ├── SceneRenderer.h
    └── SceneRenderer.cpp   ← Phase 3 inline orchestration owner
```

### 2.2 Phase 3 Method Signature

```cpp
namespace oxygen::vortex {

// Private method of SceneRenderer, defined in separate .cpp
void SceneRenderer::RenderDeferredLighting(
  RenderContext& ctx,
  const SceneTextures& scene_textures);

}  // namespace oxygen::vortex
```

### 2.3 Phase 4A Target (LightingService)

When migrated in Phase 4A, the CPU-side method moves into:

```cpp
class LightingService {
 public:
  void RenderDeferredLighting(RenderContext& ctx,
                               const SceneTextures& scene_textures);
};
```

The Phase 3 inline implementation should be written to facilitate this
migration with minimal restructuring. The CPU-side orchestration is temporary;
the shader-family home already follows the final Lighting-domain owner to avoid
future shader-path churn. Phase 4A also introduces a separately published
forward-light family under the Lighting domain, but that supporting product
does not replace the canonical per-light deferred direct-lighting contract.

### 2.4 Ownership and Lifetime

| Owner | Owned By | Lifetime |
| ----- | -------- | -------- |
| Deferred lighting logic | `SceneRenderer` (Phase 3 inline) | Per SceneRenderer |
| Light volume geometry (sphere, cone) | Renderer geometry cache | Persistent |
| Per-light PSOs | Renderer PSO cache | Persistent |

## 3. Light Types and Rendering Strategy

### 3.1 Per-Light Rendering Approach

| Light Type | Geometry | Draw Policy | Notes |
| ---------- | -------- | ----------- | ----- |
| Directional | Fullscreen triangle | Fullscreen | One fullscreen draw per directional light |
| Point | Sphere bounding volume | One-pass bounded volume | Outside-volume, inside-volume, or non-perspective bounded-volume mode |
| Spot | Cone bounding volume | One-pass bounded volume | Outside-volume, inside-volume, or non-perspective bounded-volume mode |

### 3.2 Local-Light Volume Modes (Point + Spot)

Phase 03 uses a **one-pass bounded-volume** path for every local light. The
renderer chooses the raster/depth policy per light:

- **Camera outside light volume:** render the volume’s front faces with depth
  compare `LESS_EQUAL` / `GREATER_EQUAL` depending on the active Z convention.
- **Camera inside light volume:** render back faces with depth compare
  `ALWAYS`.
- **Non-perspective view mode:** use the same one-pass bounded-volume state as
  the inside-volume mode until a dedicated orthographic local-light policy
  exists.

There is no separate stencil-mark pass in the final Phase 03 contract. Local
lights are bounded by volume rasterization and per-pixel scene-depth sampling.

### 3.3 Per-Light Data Access

Phase 3 uses a simple per-light constant buffer approach:

```cpp
struct DeferredLightConstants {
  glm::vec4 light_position_and_radius;     // xyz=position, w=radius
  glm::vec4 light_color_and_intensity;     // xyz=color, w=intensity
  glm::vec4 light_direction_and_falloff;   // xyz=direction, w=falloff
  glm::vec4 spot_angles;                   // x=inner, y=outer, zw=unused
  glm::mat4 light_world_matrix;            // Transform for volume geometry
  uint32_t light_type;                     // LIGHT_TYPE_DIRECTIONAL/POINT/SPOT
  uint32_t padding[3];
};
```

Phase 4A keeps this per-light payload model as the canonical deferred
direct-lighting path. The Lighting service adds a separately published
forward-light package for translucency and later optional optimizations, but
stage 12 does not become defined by that package.

## 4. Data Flow and Dependencies

### 4.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| SceneTextures | GBufferNormal/Material/BaseColor/CustomData (SRV) | Material data for BRDF evaluation |
| SceneTextures | SceneDepth (SRV) | Position reconstruction |
| SceneTextures | SceneColor (RTV) | Accumulation target |
| Scene | Light list (position, color, type, radius, etc.) | Per-light parameters |
| ViewConstants | View/projection matrices, camera position | Transforms |

### 4.2 Outputs

| Product | Target | Blend Mode |
| ------- | ------ | ---------- |
| SceneColor | SceneTextures::GetSceneColor() | Additive (ONE, ONE) |

### 4.3 SceneTextures State

```text
Before stage 12:
  GBufferNormal/Material/BaseColor/CustomData = SRV (readable, from stage 10 transition)
  SceneColor    = contains emissive from BasePass (stage 9)
  SceneDepth    = SRV (readable)

After stage 12:
  GBufferNormal/Material/BaseColor/CustomData = SRV (unchanged)
  SceneColor    = emissive + direct lighting accumulated
```

### 4.4 Execution Flow

```text
SceneRenderer::RenderDeferredLighting(ctx, scene_textures)
  │
  ├─ Read current view from RenderContext
  ├─ Load published SceneTextureBindings for the current view
  ├─ Bind SceneTextures as SRVs (GBufferNormal/Material/BaseColor/CustomData, SceneDepth) through the published routing metadata
  ├─ Set blend state: Additive (SrcBlend=ONE, DestBlend=ONE)
  │
  ├─ for each directional light:
  │     ├─ Upload DeferredLightConstants (type=DIRECTIONAL)
  │     ├─ Bind fullscreen directional PSO
  │     └─ Draw fullscreen triangle (3 vertices, no VB)
  │
  ├─ for each point light:
  │     ├─ Upload DeferredLightConstants (type=POINT)
  │     └─ One bounded-volume lighting draw
  │           (outside-volume, inside-volume, or non-perspective bounded-volume policy)
  │
  └─ for each spot light:
        ├─ Upload DeferredLightConstants (type=SPOT)
        └─ One bounded-volume lighting draw
              (outside-volume, inside-volume, or non-perspective bounded-volume policy)
```

## 5. Shader Contracts

### 5.1 Directional Light Shader

```hlsl
// Services/Lighting/DeferredLightDirectional.hlsl

#include "../../Shared/FullscreenTriangle.hlsli"
#include "../../Shared/DeferredShadingCommon.hlsli"
#include "../../Contracts/SceneTextures.hlsli"
#include "../../Contracts/ViewFrameBindings.hlsli"

cbuffer LightConstants : register(b1) {
  float4 LightDirection;    // xyz = direction to light, w = unused
  float4 LightColor;        // xyz = color, w = intensity
};

cbuffer ViewConstants : register(b0) {
  float4x4 ViewMatrix;
  float4x4 ProjectionMatrix;
  float4x4 InverseViewProjection;
  float3 CameraPosition;
};

FullscreenVSOutput DeferredLightDirectionalVS(uint vid : SV_VertexID) {
  return FullscreenTriangleVS(vid);
}

float4 DeferredLightDirectionalPS(FullscreenVSOutput input) : SV_Target {
  SceneTextureBindingData bindings = LoadBindingsFromCurrentView();

  float3 result = EvaluateDeferredLight(
    input.uv,
    LightDirection.xyz,
    LightColor.xyz * LightColor.w,
    1.0,  // directional lights have no attenuation
    InvViewProjection,
    CameraPosition,
    bindings);

  return float4(result, 0);
}
```

### 5.2 Point Light Shader

```hlsl
// Services/Lighting/DeferredLightPoint.hlsl

#include "DeferredLightingCommon.hlsli"

cbuffer LightConstants : register(b1) {
  float4 LightPositionAndRadius;  // xyz=position, w=radius
  float4 LightColor;              // xyz=color, w=intensity
  float4x4 LightWorldMatrix;     // Sphere volume transform
};

struct LightVolumeVSOutput {
  float4 screenPosition : TEXCOORD0;
  float4 position : SV_Position;
};

LightVolumeVSOutput DeferredLightPointVS(float3 localPos : POSITION) {
  LightVolumeVSOutput output;
  float4 worldPos = mul(LightWorldMatrix, float4(localPos, 1.0));
  output.screenPosition = mul(ProjectionMatrix, mul(ViewMatrix, worldPos));
  output.position = output.screenPosition;
  return output;
}

float4 DeferredLightPointPS(LightVolumeVSOutput input) : SV_Target {
  SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
  float2 screenUV = input.screenPosition.xy / input.screenPosition.w * 0.5 + 0.5;
  screenUV.y = 1.0 - screenUV.y;

  float3 worldPos = ReconstructWorldPosition(
    screenUV,
    SampleSceneDepth(screenUV, bindings),
    InvViewProjection);

  float3 lightVec = LightPositionAndRadius.xyz - worldPos;
  float distance = length(lightVec);
  float3 lightDir = lightVec / max(distance, 0.001);

  // Inverse square falloff with radius clamp
  float radius = LightPositionAndRadius.w;
  float attenuation = saturate(1.0 - pow(distance / radius, 4.0));
  attenuation = attenuation * attenuation / (distance * distance + 1.0);

  float3 result = EvaluateDeferredLight(
    screenUV, lightDir,
    LightColor.xyz * LightColor.w,
    attenuation,
    CameraPosition, bindings);

  return float4(result, 0);
}
```

### 5.3 Spot Light Shader

```hlsl
// Services/Lighting/DeferredLightSpot.hlsl

// Similar to point light but with angular attenuation.
// Uses cone bounding volume instead of sphere.
// Inner/outer angle falloff applied to attenuation.

float4 DeferredLightSpotPS(LightVolumeVSOutput input) : SV_Target {
  // ... same as point light with added angular falloff:
  float cosAngle = dot(-lightDir, SpotDirection.xyz);
  float angularAtt = saturate(
    (cosAngle - SpotAngles.y) / (SpotAngles.x - SpotAngles.y));
  angularAtt = angularAtt * angularAtt;

  attenuation *= angularAtt;
  // ... rest same as point light
}
```

### 5.4 DeferredLightingCommon.hlsli (Family-Local)

```hlsl
// Services/Lighting/DeferredLightingCommon.hlsli
// Family-local include — shared between point, spot, directional shaders.

#ifndef VORTEX_DEFERRED_LIGHTING_COMMON_HLSLI
#define VORTEX_DEFERRED_LIGHTING_COMMON_HLSLI

#include "../../Shared/DeferredShadingCommon.hlsli"
#include "../../Contracts/SceneTextures.hlsli"
#include "../../Contracts/ViewFrameBindings.hlsli"

cbuffer ViewConstants : register(b0) {
  float4x4 ViewMatrix;
  float4x4 ProjectionMatrix;
  float4x4 InverseViewProjection;
  float3 CameraPosition;
  uint ViewFrameBindingsSlot;
};

SceneTextureBindingData LoadBindingsFromCurrentView() {
  return LoadSceneTextureBindingsForCurrentView(ViewFrameBindingsSlot);
}

float2 ResolveDeferredLightScreenUv(float4 screenPosition) {
  float2 uv = screenPosition.xy / screenPosition.w * 0.5 + 0.5;
  uv.y = 1.0 - uv.y;
  return uv;
}

float3 ReconstructDeferredWorldPosition(float2 uv, float deviceDepth) {
  return ReconstructWorldPosition(uv, deviceDepth, InverseViewProjection);
}

#endif // VORTEX_DEFERRED_LIGHTING_COMMON_HLSLI
```

### 5.5 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexDeferredLightDirectionalVS` | vs_6_0 | Fullscreen triangle |
| `VortexDeferredLightDirectionalPS` | ps_6_0 | GBuffer read + BRDF |
| `VortexDeferredLightPointVS` | vs_6_0 | Sphere volume |
| `VortexDeferredLightPointPS` | ps_6_0 | GBuffer read + BRDF + attenuation |
| `VortexDeferredLightSpotVS` | vs_6_0 | Cone volume |
| `VortexDeferredLightSpotPS` | ps_6_0 | GBuffer read + BRDF + attenuation + angle |

## 6. Light Volume Geometry

### 6.1 Unit Sphere (Point Lights)

Phase 03 currently generates the point-light proxy sphere procedurally from
`SV_VertexID` in shader code. The sphere is still treated as a unit bounded
volume scaled and translated by `LightWorldMatrix`, but there is no persistent
VB/IB allocation in the retained runtime branch.

This is **temporary Phase 03 scaffolding only**. The permanent architecture is
Phase 4A `LightingService` ownership of persistent sphere proxy geometry.
Phase 4A must remove the procedural point-light proxy-generation path from the
canonical Stage 12 runtime path.

### 6.2 Unit Cone (Spot Lights)

Phase 03 currently generates the spot-light proxy cone procedurally from
`SV_VertexID` in shader code. The cone is still treated as a bounded volume
scaled and oriented by `LightWorldMatrix`, but there is no persistent VB/IB
allocation in the retained runtime branch.

This is **temporary Phase 03 scaffolding only**. The permanent architecture is
Phase 4A `LightingService` ownership of persistent cone proxy geometry. Phase
4A must remove the procedural spot-light proxy-generation path from the
canonical Stage 12 runtime path.

### 6.3 Fullscreen Triangle (Directional)

Generated procedurally from `SV_VertexID` — no vertex buffer needed.
Uses `FullscreenTriangle.hlsli` from Shared/.

## 7. PSO Configuration

### 7.1 Directional Light PSO

```text
RasterizerState:  CullNone (fullscreen triangle)
DepthStencil:     Depth test DISABLED (fullscreen, always shade)
                  Stencil DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (R16G16B16A16_FLOAT)
DSV:              None (no depth test)
```

### 7.2 Outside-Volume Lighting PSO (Point/Spot)

```text
RasterizerState:  CullBack (render front faces)
DepthStencil:     Depth test LESS_EQUAL / GREATER_EQUAL
                  Stencil DISABLED
                  Depth write DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (R16G16B16A16_FLOAT)
DSV:              SceneDepth (depth read)
```

### 7.3 Inside-Volume Lighting PSO (Point/Spot)

```text
RasterizerState:  CullFront (render back faces)
DepthStencil:     Depth test ALWAYS
                  Stencil DISABLED
                  Depth write DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (R16G16B16A16_FLOAT)
DSV:              SceneDepth (depth read)
```

### 7.4 Non-Perspective Bounded-Volume PSO (Point/Spot)

```text
RasterizerState:  CullFront (render back faces)
DepthStencil:     Depth test ALWAYS
                  Stencil DISABLED
                  Depth write DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (R16G16B16A16_FLOAT)
DSV:              SceneDepth (depth read)
```

The non-perspective bounded-volume mode is an explicit temporary Phase 03 policy, not a
claim that the camera is geometrically “inside” the light volume.

## 8. Stage Integration

### 8.1 Dispatch Contract

SceneRenderer calls `RenderDeferredLighting(ctx, scene_textures)` at stage 12
for the current view after setting that view in `RenderContext`. This is a
private method of SceneRenderer defined in a separate .cpp file.

### 8.2 Null-Safe Behavior

If no lights exist in the scene, the method returns immediately. SceneColor
retains only the emissive contribution from BasePass.

### 8.3 Capability Gate

Requires `kDeferredShading` + `kLightingData`. If capabilities are absent,
stage 12 is skipped.

## 9. Resource Management

### 9.1 GPU Resources

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| Procedural light-volume geometry (sphere, cone) | Shader-generated | Temporary Phase 03-only implementation shortcut; scheduled for removal in Phase 4A |
| Per-light CBV (DeferredLightConstants) | Per draw | Upload ring buffer |
| PSOs (directional + point/spot outside/inside/non-perspective variants) | Persistent | Cached by renderer |

### 9.2 Performance Considerations

- **Pass-per-light:** One draw call per light. Directional lights use a
  fullscreen triangle; point/spot lights use bounded volume draws with
  mode-specific cull/depth policy. Cost still scales linearly with light count.
- **Future optimization (Phase 4A):** Tiled/clustered deferred using compute
  shaders. Not in Phase 3 scope.
- **Temporary Phase 03 deviation only:** UE 5.7 commonly uses persistent
  bounded-volume proxy ownership for point/spot lights. The retained Vortex
  Phase 03 branch keeps the same bounded-volume contract but generates the
  sphere/cone procedurally in shader code as short-lived delivery scaffolding.
  This is not accepted as the permanent solution. The scheduled replacement is
  Phase 4A `LightingService` ownership of persistent sphere/cone proxy
  geometry.

## 10. Testability Approach

1. **Single directional light:** Render a white sphere on gray plane with
   one white directional light. Verify SceneColor shows correct
   diffuse+specular shading. Compare against reference (Lambertian +
   GGX specular).
2. **Point light bounded volume:** Place a point light with small radius.
   Verify that pixels outside the light radius show zero lighting contribution
   (only emissive).
3. **Multi-light accumulation:** Add 3 colored lights (red, green, blue).
   Verify additive accumulation produces expected color mixing.
4. **RenderDoc validation:** Inspect SceneColor after stage 12.
   Verify correct light accumulation, no banding, and correct outside-volume /
   inside-volume local-light products.

## 11. Open Questions

None. The Phase 3 contract is fully specified after fixing:

- stage-12 ownership as temporary inline SceneRenderer orchestration with a
  future LightingService migration path
- explicit published-binding access through ViewFrameBindings
- fullscreen directional vs bounded local-light rendering patterns

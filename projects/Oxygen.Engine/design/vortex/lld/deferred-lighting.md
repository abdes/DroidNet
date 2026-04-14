# Deferred Lighting LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.6
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

The deferred direct-lighting pass at stage 12 — fullscreen pass-per-light
for directional lights, stencil-bounded sphere geometry for point lights,
stencil-bounded cone geometry for spot lights. All light types read GBuffer
products, evaluate Cook-Torrance BRDF, and accumulate into SceneColor.

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

| Light Type | Geometry | Stencil | Notes |
| ---------- | -------- | ------- | ----- |
| Directional | Fullscreen triangle | None | One fullscreen draw per directional light |
| Point | Sphere bounding volume | 2-pass stencil | Mark/test to avoid lighting pixels behind the light |
| Spot | Cone bounding volume | 2-pass stencil | Similar to point but cone frustum |

### 3.2 Stencil-Bounded Lighting (Point + Spot)

Two-pass stencil approach for each local light:

**Pass 1 — Stencil mark:**

- Render back faces of light volume
- Depth test: GREATER (behind light volume back face)
- Stencil op: INCR on depth pass
- No color write

**Pass 2 — Lighting and stencil test:**

- Render front faces of light volume
- Depth test: LESS_EQUAL (in front of light volume front face)
- Stencil test: NOT_EQUAL zero (pixel was behind back face)
- Color write: additive blend to SceneColor
- Stencil op: ZERO (clear for next light)

This ensures only pixels within the light volume are shaded.

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
  Stencil       = cleared (available for stencil-bounded lighting)

After stage 12:
  GBufferNormal/Material/BaseColor/CustomData = SRV (unchanged)
  SceneColor    = emissive + direct lighting accumulated
  Stencil       = cleared (each light clears its stencil marks)
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
  │     ├─ Stencil mark pass (back faces, INCR)
  │     ├─ Lighting pass (front faces, stencil test, additive blend)
  │     └─ Stencil clear (reset to 0)
  │
  └─ for each spot light:
        ├─ Upload DeferredLightConstants (type=SPOT)
        ├─ Stencil mark pass (back faces, INCR)
        ├─ Lighting pass (front faces, stencil test, additive blend)
        └─ Stencil clear (reset to 0)
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
  float4x4 InvViewProjection;
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
  float4 position : SV_Position;
  float2 screenUV : TEXCOORD0;
};

LightVolumeVSOutput DeferredLightPointVS(float3 localPos : POSITION) {
  LightVolumeVSOutput output;
  float4 worldPos = mul(LightWorldMatrix, float4(localPos, 1.0));
  output.position = mul(ViewProjection, worldPos);
  output.screenUV = output.position.xy / output.position.w * 0.5 + 0.5;
  output.screenUV.y = 1.0 - output.screenUV.y;
  return output;
}

float4 DeferredLightPointPS(LightVolumeVSOutput input) : SV_Target {
  SceneTextureBindingData bindings = LoadBindingsFromCurrentView();

  float3 worldPos = ReconstructWorldPosition(
    input.screenUV,
    SampleSceneDepth(input.screenUV, bindings),
    InvViewProjection);

  float3 lightVec = LightPositionAndRadius.xyz - worldPos;
  float distance = length(lightVec);
  float3 lightDir = lightVec / max(distance, 0.001);

  // Inverse square falloff with radius clamp
  float radius = LightPositionAndRadius.w;
  float attenuation = saturate(1.0 - pow(distance / radius, 4.0));
  attenuation = attenuation * attenuation / (distance * distance + 1.0);

  float3 result = EvaluateDeferredLight(
    input.screenUV, lightDir,
    LightColor.xyz * LightColor.w,
    attenuation,
    InvViewProjection, CameraPosition, bindings);

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
  float4x4 ViewProjection;
  float4x4 InvViewProjection;
  float3 CameraPosition;
  uint ViewFrameBindingsSlot;
};

SceneTextureBindingData LoadBindingsFromCurrentView() {
  return LoadSceneTextureBindingsForCurrentView(ViewFrameBindingsSlot);
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

Low-poly unit sphere mesh (≈ 240 triangles). Scaled by light radius and
translated to light position via `LightWorldMatrix`. Stored in a persistent
geometry buffer allocated during renderer initialization.

### 6.2 Unit Cone (Spot Lights)

Low-poly unit cone mesh (≈ 48 triangles). Scaled and oriented by spot light
parameters (range, outer angle) via `LightWorldMatrix`.

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

### 7.2 Stencil Mark PSO (Point/Spot)

```text
RasterizerState:  CullFront (render back faces)
DepthStencil:     Depth test GREATER
                  Stencil op: INCR on depth pass, KEEP on fail
                  Depth write DISABLED
BlendState:       Color write DISABLED (stencil-only pass)
RTV:              None
DSV:              SceneDepth (stencil write)
```

### 7.3 Lighting PSO (Point/Spot)

```text
RasterizerState:  CullBack (render front faces)
DepthStencil:     Depth test LESS_EQUAL
                  Stencil test: NOT_EQUAL 0
                  Stencil op: ZERO on pass (clear for next light)
                  Depth write DISABLED
BlendState:       SrcBlend=ONE, DestBlend=ONE (additive)
RTV:              SceneColor (R16G16B16A16_FLOAT)
DSV:              SceneDepth (depth read, stencil read+write)
```

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
| Light volume VB/IB (sphere, cone) | Persistent | Allocated once at init |
| Per-light CBV (DeferredLightConstants) | Per draw | Upload ring buffer |
| PSOs (3 types × 3 light types) | Persistent | Cached by renderer |

### 9.2 Performance Considerations

- **Pass-per-light:** One draw call per directional, two draw calls per
  local light (stencil mark + lighting). For scenes with many lights, this
  scales linearly with light count.
- **Future optimization (Phase 4A):** Tiled/clustered deferred using compute
  shaders. Not in Phase 3 scope.
- **Stencil reuse:** Each local light clears its own stencil marks, so the
  stencil buffer is reused across lights without explicit clears.

## 10. Testability Approach

1. **Single directional light:** Render a white sphere on gray plane with
   one white directional light. Verify SceneColor shows correct
   diffuse+specular shading. Compare against reference (Lambertian +
   GGX specular).
2. **Point light stencil:** Place a point light with small radius. Verify
   that pixels outside the light radius show zero lighting contribution
   (only emissive).
3. **Multi-light accumulation:** Add 3 colored lights (red, green, blue).
   Verify additive accumulation produces expected color mixing.
4. **RenderDoc validation:** At frame 10, inspect SceneColor after stage 12.
   Verify correct light accumulation, no banding, no stencil artifacts.

## 11. Open Questions

None. The Phase 3 contract is fully specified after fixing:

- stage-12 ownership as temporary inline SceneRenderer orchestration with a
  future LightingService migration path
- explicit published-binding access through ViewFrameBindings
- fullscreen directional vs bounded local-light rendering patterns

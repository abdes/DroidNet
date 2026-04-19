# Deferred Lighting LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.6
**Status:** `ready`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

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
| Local-light proxy geometry | Phase 03: shader-generated procedural sphere/cone volumes; Phase 4A: `LightingService` proxy-geometry cache | Phase 03 draw-time generation; Phase 4A persistent |
| Per-light constants buffer + per-light CBV views | `SceneRenderer` (Phase 03 inline) | Per SceneRenderer allocation, per-frame contents |
| Per-light PSOs | `SceneRenderer` (Phase 03 inline), later `LightingService` | Persistent |

The key Phase 03 deviation from UE5.7 is explicit and already approved in the
architecture package: Vortex keeps UE's bounded-volume local-light algorithm,
but the retained Phase 03 branch generates point/spot proxy geometry
procedurally from `SV_VertexID` and selects per-light constants through the
bindless heap instead of using persistent renderer-owned proxy meshes plus
traditional pass-uniform plumbing. That deviation remains temporary and is
scheduled to migrate to `LightingService` in Phase 4A.

## 3. Light Types and Rendering Strategy

### 3.1 Per-Light Rendering Approach

| Light Type | Geometry | Draw Policy | Notes |
| ---------- | -------- | ----------- | ----- |
| Directional | Fullscreen triangle | Fullscreen | One fullscreen draw per directional light |
| Point | Procedural sphere bounded volume | One-pass bounded volume | Generated from `SV_VertexID`; outside-volume, inside-volume, or non-perspective bounded-volume mode |
| Spot | Procedural cone bounded volume | One-pass bounded volume | Generated from `SV_VertexID`; outside-volume, inside-volume, or non-perspective bounded-volume mode |

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

Phase 3 uses a bindless-selected per-light constant-buffer-view model.
The CPU-side contract is still a `DeferredLightConstants` struct, but shaders do
not receive it through a fixed `register(b1)` pass binding. Instead:

1. `SceneRenderer` uploads one packed `DeferredLightConstants` record per light
   into `Vortex.DeferredLight.Constants`
2. it creates one shader-visible CBV view per record
3. it passes the selected CBV index through the root constant
   `g_PassConstantsIndex`
4. the shader reads
   `ConstantBuffer<DeferredLightConstants> light_constants = ResourceDescriptorHeap[g_PassConstantsIndex];`

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

This matches the approved Vortex bindless contract and the current
`SceneRenderer` implementation. Phase 4A keeps the same per-light payload
schema but moves the CPU-side ownership into `LightingService`; it does not
change stage 12 into a forward-light-grid-driven contract.

## 4. Data Flow and Dependencies

### 4.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| Published `SceneTextureBindings` via `ViewFrameBindings` | GBufferNormal/Material/BaseColor/CustomData (SRV) | Material data for BRDF evaluation |
| Published `SceneTextureBindings` via `ViewFrameBindings` | SceneDepth (SRV) | Position reconstruction |
| SceneTextures | SceneColor (RTV) | Accumulation target |
| Scene | Light list (position, color, type, radius, etc.) | Per-light parameters |
| `ViewConstants.hlsli` globals | `view_matrix`, `projection_matrix`, `camera_position` | View-space transforms and camera data |
| Root constants | `g_PassConstantsIndex` | Selects the current light's CBV in the bindless heap |

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
  ├─ Ensure the per-light constants upload buffer and CBV views exist
  ├─ Set blend state: Additive (SrcBlend=ONE, DestBlend=ONE)
  │
  ├─ for each directional light:
  │     ├─ Write DeferredLightConstants (type=DIRECTIONAL)
  │     ├─ Set root constants {g_DrawIndex, g_PassConstantsIndex}
  │     ├─ Bind fullscreen directional PSO
  │     └─ Draw fullscreen triangle (3 vertices, no VB/IB)
  │
  ├─ for each point light:
  │     ├─ Write DeferredLightConstants (type=POINT)
  │     ├─ Set root constants {g_DrawIndex, g_PassConstantsIndex}
  │     └─ One bounded-volume lighting draw
  │           (outside-volume, inside-volume, or non-perspective bounded-volume policy;
  │            proxy sphere generated procedurally from `SV_VertexID`)
  │
  └─ for each spot light:
        ├─ Write DeferredLightConstants (type=SPOT)
        ├─ Set root constants {g_DrawIndex, g_PassConstantsIndex}
        └─ One bounded-volume lighting draw
              (outside-volume, inside-volume, or non-perspective bounded-volume policy;
               proxy cone generated procedurally from `SV_VertexID`)
```

## 5. Shader Contracts

### 5.1 Directional Light Shader

```hlsl
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

[shader("vertex")]
VortexFullscreenTriangleOutput DeferredLightDirectionalVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 DeferredLightDirectionalPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
    const float3 lighting = EvaluateDeferredLight(
        input.uv,
        VortexSafeNormalize(light_constants.light_direction_and_falloff.xyz),
        LoadDeferredLightColor(light_constants.light_color_and_intensity),
        1.0f,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}
```

### 5.2 Point Light Shader

```hlsl
[shader("vertex")]
DeferredLightVolumeVSOutput DeferredLightPointVS(uint vertex_id : SV_VertexID)
{
    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return GenerateDeferredLightVolume(
        GenerateDeferredLightSphereVertex(vertex_id),
        light_constants.light_world_matrix);
}

[shader("pixel")]
float4 DeferredLightPointPS(DeferredLightVolumeVSOutput input) : SV_Target0
{
    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
    const float2 screen_uv = ResolveDeferredLightScreenUv(input.screen_position);
    const float scene_depth = SampleSceneDepth(screen_uv, bindings);
    const float3 world_position
        = ReconstructDeferredWorldPosition(screen_uv, scene_depth);
    const float3 light_vector
        = light_constants.light_position_and_radius.xyz - world_position;
    const float attenuation = ComputeLocalLightDistanceAttenuation(
        light_vector, light_constants.light_position_and_radius.w);
    const float3 lighting = EvaluateDeferredLightAtWorldPosition(
        screen_uv,
        scene_depth,
        world_position,
        VortexSafeNormalize(light_vector),
        LoadDeferredLightColor(light_constants.light_color_and_intensity),
        attenuation,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}
```

### 5.3 Spot Light Shader

```hlsl
[shader("vertex")]
DeferredLightVolumeVSOutput DeferredLightSpotVS(uint vertex_id : SV_VertexID)
{
    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return GenerateDeferredLightVolume(
        GenerateDeferredLightConeVertex(vertex_id),
        light_constants.light_world_matrix);
}

[shader("pixel")]
float4 DeferredLightSpotPS(DeferredLightVolumeVSOutput input) : SV_Target0
{
    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
    const float2 screen_uv = ResolveDeferredLightScreenUv(input.screen_position);
    const float scene_depth = SampleSceneDepth(screen_uv, bindings);
    const float3 world_position
        = ReconstructDeferredWorldPosition(screen_uv, scene_depth);
    const float3 light_vector
        = light_constants.light_position_and_radius.xyz - world_position;
    const float base_attenuation = ComputeLocalLightDistanceAttenuation(
        light_vector, light_constants.light_position_and_radius.w);
    const float spot_attenuation = ComputeSpotLightAngularAttenuation(
        light_vector,
        light_constants.light_direction_and_falloff.xyz,
        light_constants.spot_angles.x,
        light_constants.spot_angles.y);
    const float3 lighting = EvaluateDeferredLightAtWorldPosition(
        screen_uv,
        scene_depth,
        world_position,
        VortexSafeNormalize(light_vector),
        LoadDeferredLightColor(light_constants.light_color_and_intensity),
        base_attenuation * spot_attenuation,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}
```

### 5.4 DeferredLightingCommon.hlsli (Family-Local)

```hlsl
struct DeferredLightConstants
{
    float4 light_position_and_radius;
    float4 light_color_and_intensity;
    float4 light_direction_and_falloff;
    float4 spot_angles;
    float4x4 light_world_matrix;
    uint light_type;
    uint _padding0;
    uint _padding1;
    uint _padding2;
};

// Shared helpers:
// - GenerateDeferredLightSphereVertex(vertex_id)
// - GenerateDeferredLightConeVertex(vertex_id)
// - GenerateDeferredLightVolume(local_position, light_world_matrix)
// - LoadBindingsFromCurrentView()
// - ResolveDeferredLightScreenUv(screen_position)
// - ReconstructDeferredWorldPosition(screen_uv, scene_depth)
// - ComputeLocalLightDistanceAttenuation(...)
// - ComputeSpotLightAngularAttenuation(...)
```

The include consumes `Renderer/ViewConstants.hlsli`, so view-space globals such
as `view_matrix`, `projection_matrix`, and `camera_position` come from the
standard Vortex view contract rather than from a lighting-specific `cbuffer`.
This is the approved Phase 03 bindless contract and must stay coherent with the
already-approved `SceneTextureBindings` / `ViewFrameBindings` routing model.

### 5.5 DeferredShadingCommon.hlsli (Family-Local)

`DeferredShadingCommon.hlsli` also belongs to the Lighting deferred family.
It is not renderer-wide `Shared/` code because its current responsibilities are
specific to deferred-light input validation, deferred surface reconstruction,
and deferred Cook-Torrance evaluation. It therefore lives at:

```text
Services/Lighting/DeferredShadingCommon.hlsli
```

If a future non-lighting family proves stable reuse of this file's helpers,
that promotion decision must be made explicitly against the architecture
ownership rules rather than by convenience.

### 5.6 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `DeferredLightDirectionalVS` | vs_6_0 | Fullscreen triangle |
| `DeferredLightDirectionalPS` | ps_6_0 | GBuffer read + BRDF |
| `DeferredLightPointVS` | vs_6_0 | Procedural sphere volume |
| `DeferredLightPointPS` | ps_6_0 | GBuffer read + BRDF + attenuation |
| `DeferredLightSpotVS` | vs_6_0 | Procedural cone volume |
| `DeferredLightSpotPS` | ps_6_0 | GBuffer read + BRDF + attenuation + angle |

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
| Per-light CBV views over `DeferredLightConstants` upload buffer | Per light / per draw | Upload buffer + shader-visible CBV descriptors selected through `g_PassConstantsIndex` |
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

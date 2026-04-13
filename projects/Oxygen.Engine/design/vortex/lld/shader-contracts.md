# Shader Contracts LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.7
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

The Vortex shader foundation: directory setup, shared definition files,
renderer-wide contracts (SceneTextures, GBuffer, ViewFrameBindings), shared
utility libraries (BRDF, position reconstruction, pack/unpack, fullscreen
triangle), material integration layer, and the EngineShaderCatalog
registration for all Phase 3 entrypoints.

### 1.2 Why It Is Needed

Vortex introduces a GBuffer-based deferred opaque path. Every GPU pass in
Phase 3 (depth prepass, base pass, deferred lighting) needs shared shader
contracts for scene-texture access, GBuffer encode/decode, and BRDF evaluation.
These contracts must be established before individual stage shaders are written.

### 1.3 Architectural Authority

- [ARCHITECTURE.md §10](../ARCHITECTURE.md) — shader architecture (authoritative)
- [ARCHITECTURE.md §10.2](../ARCHITECTURE.md) — six-stratum model
- [ARCHITECTURE.md §10.6](../ARCHITECTURE.md) — file-organization contract
- [ARCHITECTURE.md §10.7](../ARCHITECTURE.md) — family ownership rules

## 2. Shader Directory Structure

Per ARCHITECTURE.md §10.6, all Vortex HLSL lives under:

```text
src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/
├── Contracts/
│   ├── Definitions/
│   │   ├── SceneDefinitions.hlsli
│   │   └── LightDefinitions.hlsli
│   ├── SceneTextures.hlsli
│   ├── SceneTextureBindings.hlsli
│   ├── GBufferLayout.hlsli
│   ├── GBufferHelpers.hlsli
│   └── ViewFrameBindings.hlsli
├── Shared/
│   ├── FullscreenTriangle.hlsli
│   ├── PositionReconstruction.hlsli
│   ├── PackUnpack.hlsli
│   ├── BRDFCommon.hlsli
│   └── DeferredShadingCommon.hlsli
├── Materials/
│   ├── GBufferMaterialOutput.hlsli
│   └── MaterialTemplateAdapter.hlsli
├── Stages/
│   ├── DepthPrepass/
│   │   └── DepthPrepass.hlsl          (entrypoint)
│   └── BasePass/
│       └── BasePassGBuffer.hlsl       (entrypoint)
└── Services/
    └── Lighting/
        ├── DeferredLightDirectional.hlsl   (entrypoint)
        ├── DeferredLightPoint.hlsl         (entrypoint)
        ├── DeferredLightSpot.hlsl          (entrypoint)
        └── DeferredLightingCommon.hlsli    (family-local helper)
```

**File conventions:**

- `.hlsl` = entrypoint or multi-entry compute family
- `.hlsli` = include-only library/contract

## 3. Contracts/Definitions — Shared Data Vocabulary

### 3.1 SceneDefinitions.hlsli

Cross-language numeric/layout constants shared between C++ and HLSL.

```hlsl
// SceneDefinitions.hlsli
// Cross-language scene definitions for Vortex renderer.

#ifndef VORTEX_SCENE_DEFINITIONS_HLSLI
#define VORTEX_SCENE_DEFINITIONS_HLSLI

// GBuffer indices — must match GBufferIndex enum in C++
#define GBUFFER_A 0  // World normal (encoded)
#define GBUFFER_B 1  // Metallic, specular, roughness
#define GBUFFER_C 2  // Base color, AO
#define GBUFFER_D 3  // Custom data / shading model
#define GBUFFER_COUNT 4

// SceneTextureBindings valid_flags — must match SetupMode::Flag
#define SCENE_TEXTURE_FLAG_SCENE_DEPTH  (1u << 0)
#define SCENE_TEXTURE_FLAG_PARTIAL_DEPTH (1u << 1)
#define SCENE_TEXTURE_FLAG_VELOCITY     (1u << 2)
#define SCENE_TEXTURE_FLAG_GBUFFERS     (1u << 3)
#define SCENE_TEXTURE_FLAG_SCENE_COLOR  (1u << 4)
#define SCENE_TEXTURE_FLAG_STENCIL      (1u << 5)

// Shading model IDs (packed into GBufferB.a)
#define SHADING_MODEL_DEFAULT_LIT  0
#define SHADING_MODEL_UNLIT        1
#define SHADING_MODEL_SUBSURFACE   2
#define SHADING_MODEL_CLOTH        3

// Invalid bindless index sentinel
#define INVALID_BINDLESS_INDEX 0xFFFFFFFFu

#endif // VORTEX_SCENE_DEFINITIONS_HLSLI
```

### 3.2 LightDefinitions.hlsli

Light type IDs and per-light record layout constants.

```hlsl
// LightDefinitions.hlsli

#ifndef VORTEX_LIGHT_DEFINITIONS_HLSLI
#define VORTEX_LIGHT_DEFINITIONS_HLSLI

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

// ForwardLocalLight record: 6 x float4 = 96 bytes
// Must match ForwardLocalLight in C++
#define FORWARD_LOCAL_LIGHT_STRIDE 96

#endif // VORTEX_LIGHT_DEFINITIONS_HLSLI
```

## 4. Contracts — Renderer-Wide Shader Contracts

### 4.1 SceneTextures.hlsli

Accessor functions for reading scene-texture products.

```hlsl
// SceneTextures.hlsli
// Provides accessor functions for SceneTextures products via bindless handles.

#ifndef VORTEX_SCENE_TEXTURES_HLSLI
#define VORTEX_SCENE_TEXTURES_HLSLI

#include "Definitions/SceneDefinitions.hlsli"
#include "SceneTextureBindings.hlsli"

// Sample scene depth (returns raw depth value)
float SampleSceneDepth(float2 uv, SceneTextureBindingData bindings) {
  if (bindings.scene_depth_srv == INVALID_BINDLESS_INDEX) return 1.0;
  Texture2D depthTex = ResourceDescriptorHeap[bindings.scene_depth_srv];
  return depthTex.SampleLevel(SamplerPointClamp, uv, 0).r;
}

// Sample GBuffer by index
float4 SampleGBuffer(uint gbufferIndex, float2 uv,
                      SceneTextureBindingData bindings) {
  uint srv = bindings.gbuffer_srvs[gbufferIndex];
  if (srv == INVALID_BINDLESS_INDEX) return 0;
  Texture2D tex = ResourceDescriptorHeap[srv];
  return tex.SampleLevel(SamplerPointClamp, uv, 0);
}

// Load scene color (integer coords)
float4 LoadSceneColor(uint2 coord, SceneTextureBindingData bindings) {
  if (bindings.scene_color_srv == INVALID_BINDLESS_INDEX) return 0;
  Texture2D tex = ResourceDescriptorHeap[bindings.scene_color_srv];
  return tex.Load(uint3(coord, 0));
}

#endif // VORTEX_SCENE_TEXTURES_HLSLI
```

### 4.2 SceneTextureBindings.hlsli

Struct declaration mirroring the C++ `SceneTextureBindings`.

```hlsl
// SceneTextureBindings.hlsli

#ifndef VORTEX_SCENE_TEXTURE_BINDINGS_HLSLI
#define VORTEX_SCENE_TEXTURE_BINDINGS_HLSLI

#include "Definitions/SceneDefinitions.hlsli"

struct SceneTextureBindingData {
  uint scene_color_srv;
  uint scene_depth_srv;
  uint partial_depth_srv;
  uint velocity_srv;
  uint stencil_srv;
  uint custom_depth_srv;
  uint gbuffer_srvs[GBUFFER_COUNT];
  uint scene_color_uav;
  uint valid_flags;
};

bool IsSceneTextureValid(SceneTextureBindingData bindings, uint flag) {
  return (bindings.valid_flags & flag) != 0;
}

#endif // VORTEX_SCENE_TEXTURE_BINDINGS_HLSLI
```

### 4.3 GBufferLayout.hlsli

GBuffer format definitions and packing conventions.

```hlsl
// GBufferLayout.hlsli
// Defines the GBuffer MRT layout for Vortex deferred rendering.

#ifndef VORTEX_GBUFFER_LAYOUT_HLSLI
#define VORTEX_GBUFFER_LAYOUT_HLSLI

// MRT output structure for the base pass (deferred mode)
struct GBufferOutput {
  float4 GBufferA : SV_Target0;  // R10G10B10A2_UNORM: encoded normal
  float4 GBufferB : SV_Target1;  // R8G8B8A8_UNORM: metallic, specular,
                                  //   roughness, shading model ID
  float4 GBufferC : SV_Target2;  // R8G8B8A8_SRGB: base color RGB, AO
  float4 GBufferD : SV_Target3;  // R8G8B8A8_UNORM: custom data
  float4 Emissive : SV_Target4;  // R16G16B16A16_FLOAT: emissive → SceneColor
};

#endif // VORTEX_GBUFFER_LAYOUT_HLSLI
```

### 4.4 GBufferHelpers.hlsli

Encode/decode functions for GBuffer data.

```hlsl
// GBufferHelpers.hlsli
// Encode and decode helpers for GBuffer channels.

#ifndef VORTEX_GBUFFER_HELPERS_HLSLI
#define VORTEX_GBUFFER_HELPERS_HLSLI

#include "GBufferLayout.hlsli"
#include "../Shared/PackUnpack.hlsli"

// --- Normal encoding (octahedral, R10G10B10A2) ---

float4 EncodeGBufferNormal(float3 worldNormal) {
  float2 oct = OctahedronEncode(normalize(worldNormal));
  // Map [-1,1] to [0,1] for UNORM storage
  return float4(oct * 0.5 + 0.5, 0, 1);
}

float3 DecodeGBufferNormal(float4 gbufferA) {
  float2 oct = gbufferA.xy * 2.0 - 1.0;
  return OctahedronDecode(oct);
}

// --- Material packing (GBufferB: metallic, specular, roughness, shading model) ---

float4 EncodeGBufferMaterial(float metallic, float specular,
                              float roughness, uint shadingModel) {
  return float4(metallic, specular, roughness,
                float(shadingModel) / 255.0);
}

void DecodeGBufferMaterial(float4 gbufferB,
                            out float metallic, out float specular,
                            out float roughness, out uint shadingModel) {
  metallic = gbufferB.r;
  specular = gbufferB.g;
  roughness = gbufferB.b;
  shadingModel = uint(gbufferB.a * 255.0 + 0.5);
}

// --- Base color + AO (GBufferC) ---

float4 EncodeGBufferBaseColor(float3 baseColor, float ao) {
  return float4(baseColor, ao);
}

void DecodeGBufferBaseColor(float4 gbufferC,
                             out float3 baseColor, out float ao) {
  baseColor = gbufferC.rgb;
  ao = gbufferC.a;
}

// --- Full GBuffer read from scene textures ---

struct GBufferData {
  float3 worldNormal;
  float3 baseColor;
  float metallic;
  float specular;
  float roughness;
  float ao;
  uint shadingModel;
  float4 customData;
};

GBufferData ReadGBuffer(float2 uv, SceneTextureBindingData bindings) {
  GBufferData data;
  float4 a = SampleGBuffer(GBUFFER_A, uv, bindings);
  float4 b = SampleGBuffer(GBUFFER_B, uv, bindings);
  float4 c = SampleGBuffer(GBUFFER_C, uv, bindings);
  float4 d = SampleGBuffer(GBUFFER_D, uv, bindings);

  data.worldNormal = DecodeGBufferNormal(a);
  DecodeGBufferMaterial(b, data.metallic, data.specular,
                        data.roughness, data.shadingModel);
  DecodeGBufferBaseColor(c, data.baseColor, data.ao);
  data.customData = d;
  return data;
}
```

### 4.5 ViewFrameBindings.hlsli

Accessor for the per-view binding stack. Routes to SceneTextureBindings and
typed domain products.

```hlsl
// ViewFrameBindings.hlsli
// Per-view binding accessors routed through ViewConstants.

#ifndef VORTEX_VIEW_FRAME_BINDINGS_HLSLI
#define VORTEX_VIEW_FRAME_BINDINGS_HLSLI

#include "SceneTextureBindings.hlsli"

// ViewConstants is the root CBV entry point (slot b0).
// It carries indices to ViewFrameBindings and from there to domain payloads.
// The actual struct layout is defined by the C++ ViewConstants type.

// Access SceneTextureBindings from the per-view frame bindings.
// The caller must have ViewFrameBindings loaded; the index into the
// SceneTextureBindingData structured buffer comes from
// ViewFrameBindings::scene_texture_bindings_index.

SceneTextureBindingData LoadSceneTextureBindings(
    StructuredBuffer<SceneTextureBindingData> bindingsBuffer,
    uint index) {
  return bindingsBuffer[index];
}

#endif // VORTEX_VIEW_FRAME_BINDINGS_HLSLI
```

## 5. Shared — Renderer-Wide Utility Libraries

### 5.1 FullscreenTriangle.hlsli

Single-triangle fullscreen vertex shader used by deferred lighting, post-
process, and debug visualization passes.

```hlsl
// FullscreenTriangle.hlsli

#ifndef VORTEX_FULLSCREEN_TRIANGLE_HLSLI
#define VORTEX_FULLSCREEN_TRIANGLE_HLSLI

struct FullscreenVSOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

// Generates a fullscreen triangle from vertex ID (0, 1, 2).
// No vertex buffer needed.
FullscreenVSOutput FullscreenTriangleVS(uint vertexId : SV_VertexID) {
  FullscreenVSOutput output;
  // Cover the screen with a single triangle
  output.uv = float2((vertexId << 1) & 2, vertexId & 2);
  output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return output;
}

#endif // VORTEX_FULLSCREEN_TRIANGLE_HLSLI
```

### 5.2 PositionReconstruction.hlsli

Reconstructs world-space or view-space position from screen UV and depth.

```hlsl
// PositionReconstruction.hlsli

#ifndef VORTEX_POSITION_RECONSTRUCTION_HLSLI
#define VORTEX_POSITION_RECONSTRUCTION_HLSLI

// Reconstruct view-space position from UV and linear depth.
float3 ReconstructViewPosition(float2 uv, float linearDepth,
                                float4x4 invProjection) {
  float2 ndc = uv * 2.0 - 1.0;
  ndc.y = -ndc.y;  // Flip Y for D3D convention
  float4 clipPos = float4(ndc, 0, 1);  // Near plane
  float4 viewDir = mul(invProjection, clipPos);
  viewDir.xyz /= viewDir.w;
  return viewDir.xyz * linearDepth;
}

// Reconstruct world-space position from UV and raw depth.
float3 ReconstructWorldPosition(float2 uv, float rawDepth,
                                 float4x4 invViewProjection) {
  float2 ndc = uv * 2.0 - 1.0;
  ndc.y = -ndc.y;
  float4 clipPos = float4(ndc, rawDepth, 1.0);
  float4 worldPos = mul(invViewProjection, clipPos);
  return worldPos.xyz / worldPos.w;
}

// Linearize depth from [0,1] depth buffer to view-space distance.
float LinearizeDepth(float rawDepth, float nearZ, float farZ) {
  return nearZ * farZ / (farZ - rawDepth * (farZ - nearZ));
}

#endif // VORTEX_POSITION_RECONSTRUCTION_HLSLI
```

### 5.3 PackUnpack.hlsli

Numeric packing utilities: octahedral encoding, half-float helpers, etc.

```hlsl
// PackUnpack.hlsli

#ifndef VORTEX_PACK_UNPACK_HLSLI
#define VORTEX_PACK_UNPACK_HLSLI

// Octahedral normal encoding (Cigolle et al. 2014)
float2 OctahedronEncode(float3 n) {
  n /= (abs(n.x) + abs(n.y) + abs(n.z));
  if (n.z < 0) {
    n.xy = (1.0 - abs(n.yx)) * (n.xy >= 0 ? 1.0 : -1.0);
  }
  return n.xy;
}

float3 OctahedronDecode(float2 f) {
  float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
  if (n.z < 0) {
    n.xy = (1.0 - abs(n.yx)) * (n.xy >= 0 ? 1.0 : -1.0);
  }
  return normalize(n);
}

// Pack two [0,1] floats into one [0,1] float (8+8 bits)
float Pack2x8(float a, float b) {
  uint ua = uint(saturate(a) * 255.0);
  uint ub = uint(saturate(b) * 255.0);
  return float((ua << 8) | ub) / 65535.0;
}

void Unpack2x8(float packed, out float a, out float b) {
  uint u = uint(packed * 65535.0);
  a = float(u >> 8) / 255.0;
  b = float(u & 0xFF) / 255.0;
}

#endif // VORTEX_PACK_UNPACK_HLSLI
```

### 5.4 BRDFCommon.hlsli

Cook-Torrance BRDF evaluation shared by lighting passes.

```hlsl
// BRDFCommon.hlsli
// Core BRDF functions for Vortex deferred and forward lighting.

#ifndef VORTEX_BRDF_COMMON_HLSLI
#define VORTEX_BRDF_COMMON_HLSLI

static const float PI = 3.14159265359;

// GGX normal distribution function
float D_GGX(float NoH, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float denom = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / (PI * denom * denom);
}

// Smith-Schlick geometry function (single direction)
float G_SchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith geometry function (combined)
float G_Smith(float NdotV, float NdotL, float roughness) {
  return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
float3 F_Schlick(float cosTheta, float3 F0) {
  return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Full Cook-Torrance specular BRDF evaluation
float3 EvaluateSpecularBRDF(float3 N, float3 V, float3 L,
                             float roughness, float3 F0) {
  float3 H = normalize(V + L);
  float NdotH = max(dot(N, H), 0.0);
  float NdotV = max(dot(N, V), 0.001);
  float NdotL = max(dot(N, L), 0.0);
  float HdotV = max(dot(H, V), 0.0);

  float D = D_GGX(NdotH, roughness);
  float G = G_Smith(NdotV, NdotL, roughness);
  float3 F = F_Schlick(HdotV, F0);

  float3 numerator = D * G * F;
  float denom = 4.0 * NdotV * NdotL + 0.0001;
  return numerator / denom;
}

// Diffuse BRDF (Lambertian)
float3 EvaluateDiffuseBRDF(float3 baseColor) {
  return baseColor / PI;
}

// Combined BRDF evaluation for a single light
float3 EvaluateBRDF(float3 N, float3 V, float3 L,
                     float3 baseColor, float metallic,
                     float roughness) {
  float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
  float NdotL = max(dot(N, L), 0.0);

  float3 specular = EvaluateSpecularBRDF(N, V, L, roughness, F0);

  float3 kS = F_Schlick(max(dot(normalize(V + L), V), 0.0), F0);
  float3 kD = (1.0 - kS) * (1.0 - metallic);

  float3 diffuse = kD * EvaluateDiffuseBRDF(baseColor);

  return (diffuse + specular) * NdotL;
}

#endif // VORTEX_BRDF_COMMON_HLSLI
```

### 5.5 DeferredShadingCommon.hlsli

Common utilities specific to the deferred lighting path.

```hlsl
// DeferredShadingCommon.hlsli

#ifndef VORTEX_DEFERRED_SHADING_COMMON_HLSLI
#define VORTEX_DEFERRED_SHADING_COMMON_HLSLI

#include "BRDFCommon.hlsli"
#include "PositionReconstruction.hlsli"
#include "../Contracts/GBufferHelpers.hlsli"
#include "../Contracts/SceneTextures.hlsli"

// Full deferred lighting evaluation for one light.
// Reads GBuffer, reconstructs position, evaluates BRDF.
float3 EvaluateDeferredLight(
    float2 uv,
    float3 lightDir,      // Direction TO the light
    float3 lightColor,
    float lightAttenuation,
    float4x4 invViewProjection,
    float3 cameraPosition,
    SceneTextureBindingData bindings) {

  // Read GBuffer data
  GBufferData gbuffer = ReadGBuffer(uv, bindings);

  // Read depth and reconstruct world position
  float rawDepth = SampleSceneDepth(uv, bindings);
  float3 worldPos = ReconstructWorldPosition(uv, rawDepth, invViewProjection);

  // View direction
  float3 V = normalize(cameraPosition - worldPos);

  // Evaluate BRDF
  float3 radiance = EvaluateBRDF(
    gbuffer.worldNormal, V, lightDir,
    gbuffer.baseColor, gbuffer.metallic, gbuffer.roughness);

  return radiance * lightColor * lightAttenuation * gbuffer.ao;
}

#endif // VORTEX_DEFERRED_SHADING_COMMON_HLSLI
```

## 6. Materials — Material Integration Layer

### 6.1 GBufferMaterialOutput.hlsli

Adapter that material shaders call to pack their output into GBuffer format.

```hlsl
// GBufferMaterialOutput.hlsli

#ifndef VORTEX_GBUFFER_MATERIAL_OUTPUT_HLSLI
#define VORTEX_GBUFFER_MATERIAL_OUTPUT_HLSLI

#include "../Contracts/GBufferLayout.hlsli"
#include "../Contracts/GBufferHelpers.hlsli"

struct MaterialSurface {
  float3 worldNormal;
  float3 baseColor;
  float metallic;
  float specular;
  float roughness;
  float ao;
  float3 emissive;
  uint shadingModel;
  float4 customData;
};

GBufferOutput PackGBufferOutput(MaterialSurface surface) {
  GBufferOutput output;
  output.GBufferA = EncodeGBufferNormal(surface.worldNormal);
  output.GBufferB = EncodeGBufferMaterial(
    surface.metallic, surface.specular,
    surface.roughness, surface.shadingModel);
  output.GBufferC = EncodeGBufferBaseColor(surface.baseColor, surface.ao);
  output.GBufferD = surface.customData;
  output.Emissive = float4(surface.emissive, 0);
  return output;
}

#endif // VORTEX_GBUFFER_MATERIAL_OUTPUT_HLSLI
```

## 7. EngineShaderCatalog Registration

All Phase 3 entrypoints must be registered in `EngineShaderCatalog.h`.
Registration defines the shader request identity used by ShaderBake and
runtime cache.

### 7.1 Phase 3 Entrypoints

| Entrypoint | File | Stage | Profile |
| ---------- | ---- | ----- | ------- |
| `VortexDepthPrepassVS` | `Stages/DepthPrepass/DepthPrepass.hlsl` | vs_6_0 | Vertex |
| `VortexDepthPrepassPS` | `Stages/DepthPrepass/DepthPrepass.hlsl` | ps_6_0 | Pixel |
| `VortexBasePassVS` | `Stages/BasePass/BasePassGBuffer.hlsl` | vs_6_0 | Vertex |
| `VortexBasePassPS` | `Stages/BasePass/BasePassGBuffer.hlsl` | ps_6_0 | Pixel |
| `VortexDeferredLightDirectionalVS` | `Services/Lighting/DeferredLightDirectional.hlsl` | vs_6_0 | Fullscreen |
| `VortexDeferredLightDirectionalPS` | `Services/Lighting/DeferredLightDirectional.hlsl` | ps_6_0 | Pixel |
| `VortexDeferredLightPointVS` | `Services/Lighting/DeferredLightPoint.hlsl` | vs_6_0 | Stencil sphere |
| `VortexDeferredLightPointPS` | `Services/Lighting/DeferredLightPoint.hlsl` | ps_6_0 | Pixel |
| `VortexDeferredLightSpotVS` | `Services/Lighting/DeferredLightSpot.hlsl` | vs_6_0 | Stencil cone |
| `VortexDeferredLightSpotPS` | `Services/Lighting/DeferredLightSpot.hlsl` | ps_6_0 | Pixel |
| `VortexFullscreenTriangleVS` | shared (inline from hlsli) | vs_6_0 | Utility |

### 7.2 Catalog Registration Pattern

Per Oxygen convention (ARCHITECTURE.md §10.3), entrypoints are registered
through `EngineShaderCatalog.h`:

```cpp
// In EngineShaderCatalog.h — Phase 3 additions
catalog.Register({
  .name = "VortexDepthPrepassVS",
  .source = "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
  .entry_point = "DepthPrepassVS",
  .profile = ShaderProfile::kVS_6_0,
});
// ... repeat for each entrypoint
```

### 7.3 Permutation Strategy

Phase 3 uses minimal permutations:

| Shader | Permutations | Purpose |
| ------ | ------------ | ------- |
| Depth prepass | `HAS_VELOCITY` | Enable/disable velocity output |
| Base pass | `SHADING_MODE_FORWARD` | Deferred (default) vs forward branch |
| Deferred lighting | None initially | One variant per light type |

Permutations use `#ifdef` with catalog-managed define families. No runtime
compilation.

## 8. Build Pipeline Integration

### 8.1 ShaderBake Requirements

1. All Vortex `.hlsl` files must be discoverable through `EngineShaderCatalog.h`
   registration — no filesystem globbing.
2. Include paths must resolve `Vortex/Contracts/`, `Vortex/Shared/`, etc.
   through the existing ShaderBake include-search configuration.
3. The `Core/Bindless/` ABI headers must remain accessible.
4. Source-level include tracking and per-request dirty analysis must cover
   all Vortex includes.

### 8.2 Validation Gate

Phase 3 exit gate requires: `ShaderBake compiles all Vortex entrypoints
without error`. This is verified by running ShaderBake for all registered
Vortex catalog entries.

## 9. Testability Approach

1. **ShaderBake compilation:** All registered entrypoints compile without
   errors. This is verified as part of the build.
2. **Reflection validation:** ShaderBake reflection captures root signature
   compatibility for each entrypoint.
3. **GBuffer encode/decode round-trip:** Write a test shader that encodes
   and decodes GBuffer data, verifying lossless round-trip for normals,
   material properties, and base color.
4. **RenderDoc capture:** At frame 10, verify GBuffer textures contain
   expected encoded data (normals, materials, base color) via texture
   inspection.

## 10. Open Questions

None — shader architecture is fully specified by ARCHITECTURE.md §10. Format
decisions follow DESIGN.md §3.3. Normal encoding uses octahedral mapping
(standard for deferred renderers). BRDF follows Cook-Torrance / GGX (industry
standard).

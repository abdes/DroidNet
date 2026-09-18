//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardMesh_PS.hlsl
//! @brief Pixel shader for Forward+ physically based rendering.

#include "Vortex/Services/Environment/AerialPerspective.hlsli"
#include "Vortex/Contracts/Draw/DrawHelpers.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Contracts/Draw/MaterialShadingConstants.hlsli"
#include "Vortex/Contracts/Draw/Vertex.hlsli"
#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/View/HdrConsumerInputs.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

#include "Vortex/Contracts/Definitions/MaterialFlags.hlsli"

#include "Core/Bindless/BindlessHelpers.hlsl"
#include "Vortex/Materials/ForwardMaterialEval.hlsli"
#include "Vortex/Stages/Translucency/ForwardPbr.hlsli"
#include "Vortex/Services/Lighting/ClusterLookup.hlsli"
#include "Vortex/Services/Lighting/ForwardDirectLighting.hlsli"

float3 EnvBrdfApprox(float3 F0, float roughness, float NoV)
{
  const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
  const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
  float4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
  float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
  return F0 * AB.x + AB.y;
}

cbuffer RootConstants : register(b2, space0)
{
  uint g_DrawIndex;
  uint g_PassConstantsIndex;
}

struct ForwardEnvironmentState {
  EnvironmentStaticData data;
  GpuSkyAtmosphereParams atmosphere;
  uint has_data;
};

struct ForwardLightingTerms {
  float3 directional_direct;
  float3 positional_direct;
  float3 ibl;
  float3 direct_gates;
  float3 direct_brdf_core;
};

struct VSOutput {
  float4 position : SV_POSITION;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  float3 world_pos : TEXCOORD1;
  float3 world_normal : NORMAL;
  float3 world_tangent : TANGENT;
  float3 world_bitangent : BINORMAL;
  bool is_front_face : SV_IsFrontFace;
};

static float ResolveOutputCoverage(float material_alpha)
{
#ifdef OXYGEN_OPAQUE_OUTPUT
  return 1.0f;
#else
  return material_alpha;
#endif
}

static ForwardEnvironmentState ResolveForwardEnvironmentState()
{
  ForwardEnvironmentState state = (ForwardEnvironmentState)0;
  state.has_data = LoadEnvironmentStaticData(state.data) ? 1u : 0u;
  if (state.has_data != 0u) {
    state.atmosphere = state.data.atmosphere;
  } else {
    state.atmosphere = (GpuSkyAtmosphereParams)0;
  }
  return state;
}

static float3 ComputeForwardIblTerm(ForwardEnvironmentState env_state,
  MaterialSurface surf, float3 base_rgb, float NdotV, float3 F0,
  SamplerState linear_sampler)
{
  if (env_state.has_data == 0u || env_state.data.sky_light.enabled == 0u) {
    return 0.0.xxx;
  }

  const float3 V = surf.V;
  const float3 N = surf.N;
  const float3 cube_R = CubemapSamplingDirFromOxygenWS(reflect(-V, N));

  float3 ibl_diffuse = EvaluateStaticSkyLightDiffuseSh(env_state.data, N)
    * env_state.data.sky_light.tint_rgb
    * env_state.data.sky_light.radiance_scale
    * env_state.data.sky_light.diffuse_intensity;
  float3 ibl_specular = 0.0.xxx;

  if (env_state.data.sky_light.prefilter_map_slot != K_INVALID_BINDLESS_INDEX
    && BX_IN_TEXTURES(env_state.data.sky_light.prefilter_map_slot)) {
    TextureCube<float4> pref_map
      = ResourceDescriptorHeap[env_state.data.sky_light.prefilter_map_slot];
    ibl_specular
      = pref_map
          .SampleLevel(linear_sampler, cube_R,
            (float)env_state.data.sky_light.prefilter_max_mip * surf.roughness)
          .rgb
      * env_state.data.sky_light.tint_rgb
      * env_state.data.sky_light.radiance_scale
      * env_state.data.sky_light.specular_intensity;
  }

  float3 ibl_spec_term = 0.0.xxx;
#if defined(SKIP_BRDF_LUT)
  ibl_spec_term = ibl_specular;
#else
  if (env_state.data.sky_light.brdf_lut_slot != K_INVALID_BINDLESS_INDEX) {
    Texture2D<float2> brdf_lut
      = ResourceDescriptorHeap[env_state.data.sky_light.brdf_lut_slot];
    uint lut_w = 1u;
    uint lut_h = 1u;
    brdf_lut.GetDimensions(lut_w, lut_h);
    const float2 lut_size = float2(max(lut_w, 1u), max(lut_h, 1u));
    const float2 uv_raw = saturate(float2(NdotV, surf.roughness));
    const float2 uv = (uv_raw * (lut_size - 1.0) + 0.5) / lut_size;
    const float2 brdf = brdf_lut.SampleLevel(linear_sampler, uv, 0.0).rg;
    ibl_spec_term = ibl_specular * (F0 * brdf.x + brdf.y);
  } else {
    ibl_spec_term = ibl_specular * EnvBrdfApprox(F0, surf.roughness, NdotV);
  }
#endif

  const float3 diffuse = ibl_diffuse * base_rgb * (1.0f - surf.metalness);
  RecordForwardHdrSource(ibl_spec_term);
  RecordForwardHdrSource(diffuse);
  return ibl_spec_term + diffuse;
}

static ForwardLightingTerms ComputeForwardLightingTerms(VSOutput input,
  MaterialSurface surf, float3 shadow_normal, ForwardEnvironmentState env_state,
  SamplerState linear_sampler)
{
  ForwardLightingTerms terms = (ForwardLightingTerms)0;
  const float3 base_rgb = surf.base_rgb * input.color;
  const float3 N = surf.N;
  const float3 V = surf.V;
  const float NdotV = saturate(dot(N, V));
  const float3 F0 = lerp(float3(0.04, 0.04, 0.04), base_rgb, surf.metalness);

  terms.directional_direct
    = AccumulateDirectionalLights(input.world_pos, input.position.xy,
      env_state.atmosphere,
      shadow_normal, N, V, NdotV, F0, base_rgb, surf.metalness, surf.roughness);
  terms.positional_direct
    = AccumulateLocalLightsClustered(input.world_pos, input.position.xy,
      max(-mul(view_matrix, float4(input.world_pos, 1.0)).z, 0.0), N, V, NdotV,
      F0, base_rgb, surf.metalness, surf.roughness);
  terms.direct_gates = AccumulateDirectionalLightGatesDebug(input.world_pos,
    input.position.xy, env_state.atmosphere, shadow_normal, N, V, NdotV, F0,
    base_rgb,
    surf.metalness, surf.roughness);
  terms.direct_brdf_core
    = AccumulateDirectionalLightsBrdfCore(input.world_pos, input.position.xy,
      env_state.atmosphere, shadow_normal, N, V, NdotV, F0, base_rgb,
      surf.metalness, surf.roughness);

  terms.ibl = ComputeForwardIblTerm(
    env_state, surf, base_rgb, NdotV, F0, linear_sampler);
  return terms;
}

// Validate visible source terms without output composition or aerial perspective.
// Forward source checks require the BRDF/light/IBL terms themselves: checking
// only emission would miss cancellation between separate light contributions.
[shader("pixel")]
[earlydepthstencil]
void ValidateRadiancePS(VSOutput input)
{
  SamplerState linear_sampler = SamplerDescriptorHeap[0];
#ifdef ALPHA_TEST
  ApplyMaskedAlphaClip(EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif
  const MaterialSurface surf = EvaluateMaterialSurface(input.world_pos,
    input.world_normal, input.world_tangent, input.world_bitangent, input.uv,
    g_DrawIndex, input.is_front_face);
  RecordForwardHdrSource(surf.emissive);
  if ((surf.flags & MATERIAL_FLAG_UNLIT) != 0u) {
    RecordForwardHdrSource(surf.base_rgb * input.color);
    return;
  }
  const float3 shadow_normal = ComputeShadowSurfaceNormal(
    input.world_pos, input.world_normal, input.is_front_face);
  ComputeForwardLightingTerms(input, surf, shadow_normal,
    ResolveForwardEnvironmentState(), linear_sampler);
}

[shader("pixel")]
#if !defined(OXYGEN_OPAQUE_OUTPUT) || defined(OXYGEN_DEPTH_COMPLETE)
[earlydepthstencil]
#endif
float4 PS(VSOutput input)
  : SV_Target0
{
  SamplerState linear_sampler = SamplerDescriptorHeap[0];

#ifdef ALPHA_TEST
  ApplyMaskedAlphaClip(
    EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif

#if 1
  //=== Normal PBR Path ===
  const MaterialSurface surf = EvaluateMaterialSurface(input.world_pos,
    input.world_normal, input.world_tangent, input.world_bitangent, input.uv,
    g_DrawIndex, input.is_front_face);

#if !defined(OXYGEN_OPAQUE_OUTPUT)
  if (surf.base_a <= 0.0f) return 0.0f.xxxx;
#endif

  // Validate emission before lighting can cancel an unsupported source value.
  // Alpha rejection above precedes every status write.
#if !defined(OXYGEN_OPAQUE_OUTPUT) || defined(OXYGEN_DEPTH_COMPLETE)
  const ViewFrameBindings bindings = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
  CheckHdrStoreRange(float4(surf.emissive, 1.0f), 4u,
    bindings.exposure_status_uav, 0u, 1.0f);
#endif

  if ((surf.flags & MATERIAL_FLAG_UNLIT) != 0u) {
    RecordForwardHdrSource(surf.base_rgb * input.color);
    const float3 unlit_color = surf.base_rgb * input.color + surf.emissive;
#  if !defined(OXYGEN_OPAQUE_OUTPUT)
    RecordHdrConsumerInput(unlit_color, HDR_INPUT_TRANSLUCENCY);
#  endif
#  ifdef OXYGEN_HDR_OUTPUT
    return float4(unlit_color * GetPreExposure(), ResolveOutputCoverage(surf.base_a));
#  else
    return float4(LinearToSrgb(unlit_color * GetPreExposure()), ResolveOutputCoverage(surf.base_a));
#  endif
  }

  const float3 shadow_normal = ComputeShadowSurfaceNormal(
    input.world_pos, input.world_normal, input.is_front_face);
  const ForwardEnvironmentState env_state = ResolveForwardEnvironmentState();
  const ForwardLightingTerms lighting = ComputeForwardLightingTerms(
    input, surf, shadow_normal, env_state, linear_sampler);
  const float3 full_direct
    = lighting.directional_direct + lighting.positional_direct;

  float3 final_color = 0.0f;
#  if defined(DEBUG_DIRECT_LIGHTING_ONLY)
  final_color = AccumulateDirectionalLightsRawLambert(
    surf.N, surf.base_rgb * input.color);
#  elif defined(DEBUG_DIRECT_LIGHTING_FULL)
  final_color = full_direct;
#  elif defined(DEBUG_DIRECT_LIGHT_GATES)
  final_color = lighting.direct_gates;
#  elif defined(DEBUG_DIRECT_BRDF_CORE)
  final_color = lighting.direct_brdf_core;
#  elif defined(DEBUG_IBL_ONLY)
  final_color = lighting.ibl;
#  elif defined(DEBUG_DIRECT_PLUS_IBL)
  final_color = full_direct + lighting.ibl;
#  else
  final_color = full_direct + lighting.ibl + surf.emissive;
#  endif

#  if !defined(OXYGEN_OPAQUE_OUTPUT)
  RecordHdrConsumerInput(final_color, HDR_INPUT_TRANSLUCENCY);
#  endif
  final_color *= GetPreExposure();

  // Opaque/masked base passes receive AP once in Stage 15. Translucency runs
  // afterward and composes AP here instead.
#  if !defined(OXYGEN_OPAQUE_OUTPUT) && !defined(DEBUG_DIRECT_LIGHTING_ONLY)   \
    && !defined(DEBUG_DIRECT_LIGHTING_FULL)                                    \
    && !defined(DEBUG_DIRECT_LIGHT_GATES) && !defined(DEBUG_DIRECT_BRDF_CORE)  \
    && !defined(DEBUG_IBL_ONLY) && !defined(DEBUG_DIRECT_PLUS_IBL)
  if (env_state.has_data != 0u) {
    float3 s_dir = SafeNormalize(GetSunDirectionWS());
    if (!HasSunLight() || dot(s_dir, s_dir) < 0.5) {
      s_dir = float3(0.5, 0.707, 0.5);
    }

    RecordHdrConsumerUsage(HDR_CONSUMER_TRANSLUCENT_AP);
    const AerialPerspectiveResult ap = ComputeAerialPerspective(
      env_state.data, input.world_pos, camera_position, s_dir);
    final_color = ApplyAerialPerspective(final_color, ap);
  }
#  endif

#  ifdef OXYGEN_HDR_OUTPUT
  return float4(final_color, ResolveOutputCoverage(surf.base_a));
#  else
  return float4(LinearToSrgb(final_color), ResolveOutputCoverage(surf.base_a));
#  endif
#endif
}

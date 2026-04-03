//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardMesh_PS.hlsl
//! @brief Pixel shader for Forward+ physically based rendering.

#include "Atmosphere/AerialPerspective.hlsli"
#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/LightingHelpers.hlsli"
#include "Renderer/MaskedAlphaTest.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Renderer/Vertex.hlsli"
#include "Renderer/ViewColorHelpers.hlsli"
#include "Renderer/ViewConstants.hlsli"

#include "MaterialFlags.hlsli"

#include "Core/Bindless/BindlessHelpers.hlsl"
#include "Forward/ForwardDirectLighting.hlsli"
#include "Forward/ForwardMaterialEval.hlsli"
#include "Forward/ForwardPbr.hlsli"
#include "Lighting/ClusterLookup.hlsli"

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

  uint sky_slot = env_state.data.sky_light.cubemap_slot;
  if (sky_slot == K_INVALID_BINDLESS_INDEX) {
    sky_slot = env_state.data.sky_sphere.cubemap_slot;
  }
  if (sky_slot == K_INVALID_BINDLESS_INDEX) {
    return 0.0.xxx;
  }

  const float3 V = surf.V;
  const float3 N = surf.N;
  const float3 cube_R = CubemapSamplingDirFromOxygenWS(reflect(-V, N));
  const float3 cube_N = CubemapSamplingDirFromOxygenWS(N);

  float3 ibl_diffuse = 0.0.xxx;
  float3 ibl_specular = 0.0.xxx;
  TextureCube<float4> sky_cube = ResourceDescriptorHeap[sky_slot];

  if (env_state.data.sky_light.irradiance_map_slot != K_INVALID_BINDLESS_INDEX
    && env_state.data.sky_light.prefilter_map_slot
      != K_INVALID_BINDLESS_INDEX) {
    TextureCube<float4> irr_map
      = ResourceDescriptorHeap[env_state.data.sky_light.irradiance_map_slot];
    TextureCube<float4> pref_map
      = ResourceDescriptorHeap[env_state.data.sky_light.prefilter_map_slot];
    ibl_diffuse = irr_map.SampleLevel(linear_sampler, cube_N, 0.0).rgb
      * env_state.data.sky_light.tint_rgb
      * env_state.data.sky_light.radiance_scale
      * env_state.data.sky_light.diffuse_intensity;
    ibl_specular
      = pref_map
          .SampleLevel(linear_sampler, cube_R,
            (float)env_state.data.sky_light.prefilter_max_mip * surf.roughness)
          .rgb
      * env_state.data.sky_light.tint_rgb
      * env_state.data.sky_light.radiance_scale
      * env_state.data.sky_light.specular_intensity;
  } else {
    const float sky_max_mip
      = (sky_slot == env_state.data.sky_sphere.cubemap_slot)
      ? (float)env_state.data.sky_sphere.cubemap_max_mip
      : (float)env_state.data.sky_light.cubemap_max_mip;
    ibl_specular
      = sky_cube
          .SampleLevel(linear_sampler, cube_R, sky_max_mip * surf.roughness)
          .rgb
      * env_state.data.sky_light.tint_rgb
      * env_state.data.sky_light.radiance_scale
      * env_state.data.sky_light.specular_intensity;
    ibl_diffuse = sky_cube.SampleLevel(linear_sampler, cube_N, sky_max_mip).rgb
      * env_state.data.sky_light.tint_rgb
      * env_state.data.sky_light.radiance_scale
      * env_state.data.sky_light.diffuse_intensity;
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

  return ibl_spec_term + ibl_diffuse * base_rgb * (1.0f - surf.metalness);
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
    = AccumulatePositionalLightsClustered(input.world_pos, input.position.xy,
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

[shader("pixel")] float4 PS(VSOutput input)
  : SV_Target0
{
  SamplerState linear_sampler = SamplerDescriptorHeap[0];
  const float3 shadow_normal = ComputeShadowSurfaceNormal(
    input.world_pos, input.world_normal, input.is_front_face);

#ifdef ALPHA_TEST
  ApplyMaskedAlphaClip(
    EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif

#if 1
  //=== Normal PBR Path ===
  const MaterialSurface surf = EvaluateMaterialSurface(input.world_pos,
    input.world_normal, input.world_tangent, input.world_bitangent, input.uv,
    g_DrawIndex, input.is_front_face);
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

#  if !defined(DEBUG_DIRECT_LIGHTING_ONLY)                                     \
    && !defined(DEBUG_DIRECT_LIGHTING_FULL)                                    \
    && !defined(DEBUG_DIRECT_LIGHT_GATES) && !defined(DEBUG_DIRECT_BRDF_CORE)  \
    && !defined(DEBUG_IBL_ONLY) && !defined(DEBUG_DIRECT_PLUS_IBL)
  if (env_state.has_data != 0u) {
    float3 s_dir = SafeNormalize(GetSunDirectionWS());
    if (!HasSunLight() || dot(s_dir, s_dir) < 0.5) {
      s_dir = float3(0.5, 0.707, 0.5);
    }

    const AerialPerspectiveResult ap = ComputeAerialPerspective(
      env_state.data, input.world_pos, camera_position, s_dir);
    final_color = ApplyAerialPerspective(final_color, ap);
  }
#  endif

#  ifdef OXYGEN_HDR_OUTPUT
  return float4(final_color, surf.base_a);
#  else
#    if !defined(DEBUG_DIRECT_LIGHTING_ONLY)                                   \
      && !defined(DEBUG_DIRECT_LIGHTING_FULL)                                  \
      && !defined(DEBUG_DIRECT_LIGHT_GATES)                                    \
      && !defined(DEBUG_DIRECT_BRDF_CORE) && !defined(DEBUG_IBL_ONLY)          \
      && !defined(DEBUG_DIRECT_PLUS_IBL)
  final_color *= GetExposure();
#    endif
  return float4(LinearToSrgb(final_color), surf.base_a);
#  endif
#endif
}

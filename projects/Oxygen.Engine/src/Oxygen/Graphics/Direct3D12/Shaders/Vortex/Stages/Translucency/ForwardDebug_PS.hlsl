//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardDebug_PS.hlsl
//! @brief Pixel shader for Forward+ diagnostic and LDR debug visualization
//! modes.

#include "Vortex/Services/Diagnostics/DebugHelpers.hlsli"
#include "Vortex/Contracts/Draw/DrawHelpers.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Contracts/Draw/MaterialShadingConstants.hlsli"
#include "Vortex/Contracts/Shadows/ShadowHelpers.hlsli"
#include "Vortex/Contracts/Draw/Vertex.hlsli"
#include "Vortex/Contracts/View/ViewColorHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Shadows/VsmFrameBindings.hlsli"

#include "Vortex/Contracts/Definitions/MaterialFlags.hlsli"

#include "Core/Bindless/BindlessHelpers.hlsl"
#include "Vortex/Materials/ForwardMaterialEval.hlsli"
#include "Vortex/Stages/Translucency/ForwardPbr.hlsli"
#include "Vortex/Services/Lighting/ClusterLookup.hlsli"

cbuffer RootConstants : register(b2, space0)
{
  uint g_DrawIndex;
  uint g_PassConstantsIndex;
}

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

static inline float3 ComputeSceneDebugUnderlay(VSOutput input)
{
  MaterialSurface s = EvaluateMaterialSurface(input.world_pos,
    input.world_normal, input.world_tangent, input.world_bitangent, input.uv,
    g_DrawIndex, input.is_front_face);

  const float3 N = SafeNormalize(s.N);
  const float3 V = SafeNormalize(camera_position - input.world_pos);
  const float3 L = SafeNormalize(float3(0.45f, -0.35f, 0.82f));
  const float ndotl = saturate(dot(N, L));
  const float rim = pow(1.0f - saturate(dot(N, V)), 2.0f);
  const float luminance
    = saturate(dot(s.base_rgb, float3(0.299f, 0.587f, 0.114f)));
  const float shade
    = saturate(0.16f + luminance * (0.24f + 0.60f * ndotl) + 0.10f * rim);
  return shade.xxx;
}

static inline float3 GetIblFaceColor(uint face_index)
{
  switch (face_index) {
  case 0:
    return float3(1.0, 0.0, 0.0); // +X
  case 1:
    return float3(0.0, 1.0, 0.0); // -X
  case 2:
    return float3(0.0, 0.0, 1.0); // +Y
  case 3:
    return float3(1.0, 1.0, 0.0); // -Y
  case 4:
    return float3(0.0, 1.0, 1.0); // +Z
  case 5:
    return float3(1.0, 0.0, 1.0); // -Z
  default:
    return float3(1.0, 1.0, 1.0);
  }
}

static inline void CubemapDirToFaceUv(
  float3 dir, out uint face_index, out float2 uv)
{
  float3 a = abs(dir);
  float s = 0.0;
  float t = 0.0;

  if (a.x >= a.y && a.x >= a.z) {
    if (dir.x >= 0.0) {
      face_index = 0u; // +X
      s = -dir.z / a.x;
      t = dir.y / a.x;
    } else {
      face_index = 1u; // -X
      s = dir.z / a.x;
      t = dir.y / a.x;
    }
  } else if (a.y >= a.x && a.y >= a.z) {
    if (dir.y >= 0.0) {
      face_index = 2u; // +Y
      s = dir.x / a.y;
      t = -dir.z / a.y;
    } else {
      face_index = 3u; // -Y
      s = dir.x / a.y;
      t = dir.z / a.y;
    }
  } else {
    if (dir.z >= 0.0) {
      face_index = 4u; // +Z
      s = dir.x / a.z;
      t = dir.y / a.z;
    } else {
      face_index = 5u; // -Z
      s = -dir.x / a.z;
      t = dir.y / a.z;
    }
  }

  uv = float2(0.5 * (s + 1.0), 0.5 * (1.0 - t));
}

static inline float3 MakeIblDebugColor(float3 dir, bool include_grid)
{
  uint face_index = 0u;
  float2 uv = 0.0;
  CubemapDirToFaceUv(normalize(dir), face_index, uv);

  float3 base = GetIblFaceColor(face_index);
  float grid_line = 0.0;
  if (include_grid) {
    float2 grid = frac(uv * 8.0);
    grid_line += step(grid.x, 0.02);
    grid_line += step(grid.y, 0.02);
    grid_line += step(0.98, grid.x);
    grid_line += step(0.98, grid.y);
    grid_line = saturate(grid_line);
  }

  float3 color = lerp(base, float3(1.0, 1.0, 1.0), grid_line);
  return color;
}

static const float3 kDebugErrorColor = float3(1.0f, 0.0f, 1.0f);

static inline bool TryLoadSceneDepth(
  float2 screen_position, out float scene_depth)
{
  scene_depth = 0.0f;

  const ViewFrameBindings view_bindings
    = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
  if (view_bindings.scene_depth_slot == K_INVALID_BINDLESS_INDEX) {
    return false;
  }

  Texture2D<float> scene_depth_texture
    = ResourceDescriptorHeap[view_bindings.scene_depth_slot];
  uint scene_depth_width = 0u;
  uint scene_depth_height = 0u;
  scene_depth_texture.GetDimensions(scene_depth_width, scene_depth_height);
  if (scene_depth_width == 0u || scene_depth_height == 0u) {
    return false;
  }

  const uint2 pixel = min(uint2(screen_position),
    uint2(max(1u, scene_depth_width) - 1u, max(1u, scene_depth_height) - 1u));
  scene_depth = scene_depth_texture.Load(int3(pixel, 0));
  return true;
}

static inline float ReconstructLinearEyeDepth(float device_depth)
{
  const float projection_zz = projection_matrix._33;
  const float projection_zw = projection_matrix._34;
  const float projection_wz = projection_matrix._43;
  const float epsilon = 1.0e-6f;

  // Perspective projections keep row 3, column 4 at -1. Orthographic
  // projections keep it at 0 and encode the depth offset in row 4, column 3.
  if (abs(projection_zw) > 0.5f) {
    return max(
      projection_wz / max(device_depth + projection_zz, epsilon), 0.0f);
  }
  if (abs(projection_zz) <= epsilon) {
    return 0.0f;
  }

  return max((projection_wz - device_depth) / projection_zz, 0.0f);
}

static inline float3 MakeDepthMismatchHeatmap(float depth_error)
{
  const float match_epsilon = 1.0e-5f;
  if (depth_error <= match_epsilon) {
    return 0.0.xxx;
  }

  const float ramp
    = saturate((depth_error - match_epsilon) / (16.0f * match_epsilon));
  if (ramp < 0.5f) {
    return lerp(0.0.xxx, float3(1.0f, 1.0f, 0.0f), ramp * 2.0f);
  }
  return lerp(
    float3(1.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f), (ramp - 0.5f) * 2.0f);
}

[shader("pixel")] float4 PS(VSOutput input)
  : SV_Target0
{
  SamplerState linear_sampler = SamplerDescriptorHeap[0];
#if defined(ALPHA_TEST) || defined(DEBUG_MASKED_ALPHA_COVERAGE)
  const MaskedAlphaTestResult alpha_test
    = EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler);
#endif

#if defined(ALPHA_TEST) && !defined(DEBUG_MASKED_ALPHA_COVERAGE)
  ApplyMaskedAlphaClip(alpha_test);
#endif

  float3 debug_out = 0.0f;
  bool debug_handled = false;
  const float3 shadow_normal = ComputeShadowSurfaceNormal(
    input.world_pos, input.world_normal, input.is_front_face);

#if defined(DEBUG_SCENE_DEPTH_RAW) || defined(DEBUG_SCENE_DEPTH_LINEAR)        \
  || defined(DEBUG_SCENE_DEPTH_MISMATCH)
  {
    float scene_depth = 0.0f;
    if (!TryLoadSceneDepth(input.position.xy, scene_depth)) {
      debug_out = kDebugErrorColor;
    } else {
#  if defined(DEBUG_SCENE_DEPTH_RAW)
      debug_out = saturate(scene_depth).xxx;
#  elif defined(DEBUG_SCENE_DEPTH_LINEAR)
      const float linear_eye_depth = ReconstructLinearEyeDepth(scene_depth);
      const float display = saturate(1.0f / (1.0f + linear_eye_depth));
      debug_out = display.xxx;
#  else
      debug_out = MakeDepthMismatchHeatmap(abs(input.position.z - scene_depth));
#  endif
    }
    debug_handled = true;
  }
#elif defined(DEBUG_MASKED_ALPHA_COVERAGE)
  if (!alpha_test.has_material_data) {
    debug_out = kDebugErrorColor;
  } else if (!alpha_test.alpha_test_enabled) {
    debug_out = 0.15f.xxx;
  } else {
    debug_out = alpha_test.alpha >= alpha_test.cutoff
      ? float3(0.0f, 1.0f, 0.0f)
      : float3(1.0f, 0.0f, 0.0f);
  }
  debug_handled = true;
#elif defined(DEBUG_BASE_COLOR) || defined(DEBUG_WORLD_NORMALS)                \
  || defined(DEBUG_ROUGHNESS) || defined(DEBUG_METALNESS)
  MaterialSurface s = EvaluateMaterialSurface(input.world_pos,
    input.world_normal, input.world_tangent, input.world_bitangent, input.uv,
    g_DrawIndex, input.is_front_face);
#  if defined(DEBUG_BASE_COLOR)
  debug_out = s.base_rgb * input.color;
#  elif defined(DEBUG_WORLD_NORMALS)
  debug_out = s.N * 0.5f + 0.5f;
#  elif defined(DEBUG_ROUGHNESS)
  debug_out = s.roughness.xxx;
#  elif defined(DEBUG_METALNESS)
  debug_out = s.metalness.xxx;
#  endif
  debug_handled = true;
#elif defined(DEBUG_IBL_SPECULAR) || defined(DEBUG_IBL_RAW_SKY)                \
  || defined(DEBUG_IBL_IRRADIANCE) || defined(DEBUG_IBL_FACE_INDEX)
  const float3 N_v = SafeNormalize(input.world_normal);
  const float3 V_v = SafeNormalize(camera_position - input.world_pos);
  const float3 cube_R = CubemapSamplingDirFromOxygenWS(reflect(-V_v, N_v));
  const float3 cube_N = CubemapSamplingDirFromOxygenWS(N_v);

  if (dot(N_v, N_v) < 0.5 || dot(V_v, V_v) < 0.5) {
    debug_out = kDebugErrorColor;
    debug_handled = true;
  }

  if (!debug_handled) {
#  if defined(DEBUG_IBL_SPECULAR)
    debug_out = MakeIblDebugColor(cube_R, true);
    debug_handled = true;
#  elif defined(DEBUG_IBL_IRRADIANCE)
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    const float screen_w = max(1.0,
      (float)lighting.grid_size.x * 64.0f);
    const bool show_world = (input.position.x < 0.5 * screen_w);
    if (show_world) {
      debug_out = input.world_normal * 0.5 + 0.5;
    } else {
      debug_out = MakeIblDebugColor(cube_N, false);
    }
    debug_handled = true;
#  elif defined(DEBUG_IBL_FACE_INDEX)
    {
      uint face_index = 0u;
      float2 uv = 0.0;
      CubemapDirToFaceUv(normalize(cube_R), face_index, uv);
      debug_out = GetIblFaceColor(face_index);
      debug_handled = true;
    }
#  else
    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (LoadEnvironmentStaticData(env_data) && env_data.sky_light.enabled) {
      uint slot = env_data.sky_light.cubemap_slot;
      if (slot != K_INVALID_BINDLESS_INDEX && BX_IN_GLOBAL_SRV(slot)) {
        TextureCube<float4> sky_cube = ResourceDescriptorHeap[slot];
        float3 raw = sky_cube.SampleLevel(linear_sampler, cube_R, 0.0).rgb;
        debug_out = raw * env_data.sky_light.radiance_scale;
        debug_handled = true;
      }
    }
#  endif
  }
#elif defined(DEBUG_VIRTUAL_SHADOW_MASK)
  {
    const ViewFrameBindings view_bindings
      = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    const VsmFrameBindings vsm_bindings
      = LoadVsmFrameBindings(view_bindings.virtual_shadow_frame_slot);
    if (vsm_bindings.screen_shadow_mask_slot == K_INVALID_BINDLESS_INDEX) {
      debug_out = kDebugErrorColor;
    } else {
      Texture2D<float> shadow_mask
        = ResourceDescriptorHeap[vsm_bindings.screen_shadow_mask_slot];
      uint shadow_mask_width = 0u;
      uint shadow_mask_height = 0u;
      shadow_mask.GetDimensions(shadow_mask_width, shadow_mask_height);
      const uint2 pixel = min(uint2(input.position.xy),
        uint2(
          max(1u, shadow_mask_width) - 1u, max(1u, shadow_mask_height) - 1u));
      const float visibility = saturate(shadow_mask.Load(int3(pixel, 0)));
      debug_out = visibility.xxx;
    }
    debug_handled = true;
  }
#endif

  if (!debug_handled) {
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    const uint grid = lighting.grid_indirection_srv;
    if (grid != K_INVALID_BINDLESS_INDEX) {
      float linear_depth
        = max(-mul(view_matrix, float4(input.world_pos, 1.0)).z, 0.0);
      uint idx = GetClusterIndex(input.position.xy, linear_depth);
      uint3 dims = GetClusterDimensions();
#if defined(DEBUG_LIGHT_HEATMAP)
      debug_out = HeatMapColor(
        saturate((float)GetClusterLightInfo(grid, idx).light_count
          / (float)max(lighting.max_culled_lights_per_cell, 1u)));
#elif defined(DEBUG_DEPTH_SLICE)
      debug_out = DepthSliceColor(idx / (dims.x * dims.y), dims.z);
#elif defined(DEBUG_CLUSTER_INDEX)
      debug_out = ClusterIndexColor(uint3(uint(input.position.x)
          / 64u,
        uint(input.position.y)
          / 64u,
        idx / (dims.x * dims.y)));
#endif
    }
  }

#if defined(DEBUG_IBL_RAW_SKY)
  debug_out *= GetExposure();
#endif

#ifdef OXYGEN_HDR_OUTPUT
  return float4(debug_out, 1.0f);
#else
  return float4(LinearToSrgb(debug_out), 1.0f);
#endif
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Sphere Pixel Shader
//!
//! Renders the sky background with priority:
//! 1. SkyAtmosphere (procedural) - if enabled and LUTs available
//! 2. SkySphere cubemap - if enabled and source is kCubemap
//! 3. SkySphere solid color - if enabled and source is kSolidColor
//! 4. Black fallback

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Common/Geometry.hlsli"
#include "Renderer/GpuDebug.hlsli"
#include "Atmosphere/SkyColor.hlsli"

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Output topology: triangle list (3 vertices).
struct SkyPassConstants {
    float2 mouse_down_position;
    float2 viewport_size;
    uint mouse_down_valid;
    uint depth_srv_index; // Updated index
    uint pad1;
    uint pad2;
    float4x4 inv_view_proj; // New matrix
};

//! Input from vertex shader.
struct SkyPSInput {
    float4 position : SV_POSITION;
    float3 view_dir : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float RaymarchQuadraticSampleT(uint step_index, uint step_count, float t_max, float sample_segment_t)
{
    // Quadratic distribution across [0, t_max] with the sample taken within
    // each segment at `sample_segment_t` (e.g. 0.3).
    float t0 = float(step_index) / float(step_count);
    float t1 = float(step_index + 1u) / float(step_count);
    t0 *= t0;
    t1 *= t1;
    t0 *= t_max;
    t1 *= t_max;
    return t0 + (t1 - t0) * sample_segment_t;
}

void EmitSkyRayMarchDebug(EnvironmentStaticData env_data, float3 ray_origin, float3 ray_dir)
{
    const float3 planet_center = GetPlanetCenterWS();
    const float3 ray_origin_planet = ray_origin - planet_center;
    const float planet_radius = env_data.atmosphere.planet_radius_m;
    const float atmosphere_radius = planet_radius + env_data.atmosphere.atmosphere_height_m;

    float t_bottom = RaySphereIntersectNearest(ray_origin_planet, ray_dir, planet_radius);
    float t_top = RaySphereIntersectNearest(ray_origin_planet, ray_dir, atmosphere_radius);
    float t_max = 0.0f;
    if (t_bottom < 0.0) {
        if (t_top < 0.0) {
            t_max = 0.0f;
        } else {
            t_max = t_top;
        }
    } else {
        if (t_top > 0.0) {
            t_max = min(t_top, t_bottom);
        }
    }

    if (t_max <= 0.0) { return; }

    const float kMaxDebugDistance = 9000000.0f;
    float t_max_clamped = min(t_max, kMaxDebugDistance);
    AddGpuDebugLine(ray_origin, ray_origin + ray_dir * t_max_clamped, float3(1.0f, 1.0f, 0.0f));
    if (t_max != t_max_clamped) {
        AddGpuDebugLine(ray_origin, ray_origin + ray_dir * t_max_clamped, float3(1.0f, 0.0f, 1.0f));
    }
    t_max = t_max_clamped;

    const uint kSteps = 64u;
    const float sample_segment_t = 0.3f;
    float t_prev = 0.0f;

    [loop]
    for (uint i = 0u; i < kSteps; ++i) {
        float t = RaymarchQuadraticSampleT(i, kSteps, t_max, sample_segment_t);

        float3 p_prev = ray_origin_planet + ray_dir * t_prev;
        float3 p = ray_origin_planet + ray_dir * t;
        const float3 debug_offset = float3(0.0f, 0.0f, -planet_radius);
        float3 debug_p_prev = debug_offset + p_prev;
        float3 debug_p = debug_offset + p;

        if (any(abs(p) > kMaxDebugDistance)) {
            AddGpuDebugLine(ray_origin, ray_origin + ray_dir * 1000.0f, float3(1.0f, 0.0f, 1.0f));
            break;
        }

        AddGpuDebugLine(debug_p_prev, debug_p, float3(0.0f, 1.0f, 0.0f));
        const bool emit_cross = (i == 0u) || ((i % 10u) == 0u);
        if (emit_cross) {
            AddGpuDebugCross(debug_p, float3(0.0f, 0.4f, 1.0f), 0.05f);
        }

        t_prev = t;
    }
}

void TryEmitSkyRayMarchDebug(EnvironmentStaticData env_data, float2 pixel, float3 ray_origin, float3 ray_dir)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    ConstantBuffer<SkyPassConstants> pass_buffer = ResourceDescriptorHeap[g_PassConstantsIndex];
    SkyPassConstants pass = pass_buffer;

    // Gating: only emit debug for the picked pixel.
    if (pass.mouse_down_valid == 0u) {
        return;
    }
    const uint2 pixel_u = uint2(pixel);
    const uint2 mouse_u = uint2(pass.mouse_down_position + 0.5);
    if (!all(pixel_u == mouse_u)) {
        return;
    }

    EmitSkyRayMarchDebug(env_data, ray_origin, ray_dir);
}

[shader("pixel")]
float4 PS(SkyPSInput input) : SV_TARGET
{
    // Load environment static data.
    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 view_dir = normalize(input.view_dir);

    // Depth-aware ray termination [Phase 4]
    // If the sky pass is drawn with depth test disabled or forced always,
    // this allows clamping the atmosphere contribution to the scene geometry.
    float max_dist = 1e9; // Infinity

    if (g_PassConstantsIndex != K_INVALID_BINDLESS_INDEX)
    {
         ConstantBuffer<SkyPassConstants> pass = ResourceDescriptorHeap[g_PassConstantsIndex];

         if (pass.depth_srv_index != K_INVALID_BINDLESS_INDEX)
         {
             Texture2D<float> depth_tex = ResourceDescriptorHeap[pass.depth_srv_index];
             int2 load_coord = int2(input.position.xy);

             // Clamp to viewport to avoid out-of-bounds load on resize edge cases
             uint w, h;
             depth_tex.GetDimensions(w, h);
             if (load_coord.x < w && load_coord.y < h)
             {
                 float depth = depth_tex.Load(int3(load_coord, 0));

                 // If not background, calculate world position distance
                 // Note: Depends on clear depth. Assuming D3D12 standard Far=1.0.
                 if (depth < 1.0)
                 {
                     float2 uv = input.position.xy / pass.viewport_size;
                     float2 ndc = uv * 2.0 - 1.0;
                     ndc.y = -ndc.y; // D3D Y-flip

                     float4 h_pos = mul(pass.inv_view_proj, float4(ndc, depth, 1.0));
                     float3 world_pos = h_pos.xyz / h_pos.w;

                     max_dist = length(world_pos - camera_position);
                 }
             }
         }

         TryEmitSkyRayMarchDebug(env_data, input.position.xy, camera_position, view_dir);
    }

    // Compute linear sky color
    // Note: ComputeSkyColor uses LUTs which are pre-integrated to infinity.
    // To support max_dist clamping properly with LUTs, one would need to subtract
    // the transmittance-weighted scattered light beyond max_dist, which requires
    // more complex LUT usage (e.g. 4D LUTs or reprojection).
    // For now, this prepares the shader for such logic or analytic fallbacks.
    float3 sky_color = ComputeSkyColor(env_data, view_dir);

    return float4(sky_color, 1.0f);
}

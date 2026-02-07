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
#include "Atmosphere/SkySphereCommon.hlsli"
#include "Renderer/GpuDebug.hlsli"

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct SkyPassConstants
{
    float2 mouse_down_position;
    float2 viewport_size;
    uint mouse_down_valid;
    uint pad0;
    uint pad1;
    uint pad2;
};

float RaySphereIntersectNearest(float3 origin, float3 dir, float3 center, float radius)
{
    float a = dot(dir, dir);
    float3 center_to_origin = origin - center;
    float b = 2.0 * dot(dir, center_to_origin);
    float c = dot(center_to_origin, center_to_origin) - radius * radius;
    float delta = b * b - 4.0 * a * c;
    if (delta < 0.0 || a == 0.0)
    {
        return -1.0;
    }
    float sol0 = (-b - sqrt(delta)) / (2.0 * a);
    float sol1 = (-b + sqrt(delta)) / (2.0 * a);
    if (sol0 < 0.0 && sol1 < 0.0)
    {
        return -1.0;
    }
    if (sol0 < 0.0)
    {
        return max(0.0, sol1);
    }
    if (sol1 < 0.0)
    {
        return max(0.0, sol0);
    }
    return max(0.0, min(sol0, sol1));
}

//! Input from vertex shader.
struct SkyPSInput
{
    float4 position : SV_POSITION;
    float3 view_dir : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 PS(SkyPSInput input) : SV_TARGET
{
    const float line_half_width = max(fwidth(input.uv.x) * 1.5, 1e-4);
    if (abs(input.uv.x - 0.5) <= line_half_width)
    {
        return float4(1.0f, 0.0f, 0.0f, 1.0f);
    }

    // Load environment static data.
    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 view_dir = normalize(input.view_dir);

    if (g_PassConstantsIndex != K_INVALID_BINDLESS_INDEX)
    {
        ConstantBuffer<SkyPassConstants> pass_buffer
            = ResourceDescriptorHeap[g_PassConstantsIndex];
        SkyPassConstants pass = pass_buffer;
        const float3 ray_origin = camera_position;
        float3 view_dir_vs = normalize(float3(
            0.0 / projection_matrix[0][0],
            0.0 / projection_matrix[1][1],
            -1.0));
        float3x3 inv_view_rot = transpose((float3x3)view_matrix);
        float3 center_dir = normalize(mul(inv_view_rot, view_dir_vs));
        if (pass.mouse_down_valid != 0u)
        {
            const float2 pixel = input.position.xy;
            const uint2 pixel_u = uint2(pixel);
            const uint2 mouse_u = uint2(pass.mouse_down_position + 0.5);
            const bool debug_enabled = all(pixel_u == mouse_u);
            if (debug_enabled)
            {
                const float3 ray_dir = view_dir;
                float3 mouse_point = ray_origin + ray_dir * 50.0;
                float3 center_point = ray_origin + center_dir * 150.0;
                AddGpuDebugLine(mouse_point, center_point, float3(1.0, 0.0, 0.0));
                const float3 planet_center = GetPlanetCenterWS();
                const float3 ray_origin_planet = ray_origin - planet_center;
                const float planet_radius = env_data.atmosphere.planet_radius_m;
                const float atmosphere_radius
                    = planet_radius + env_data.atmosphere.atmosphere_height_m;
                const float3 earth_origin = float3(0.0, 0.0, 0.0);

                float t_bottom = RaySphereIntersectNearest(
                    ray_origin_planet, ray_dir, earth_origin, planet_radius);
                float t_top = RaySphereIntersectNearest(
                    ray_origin_planet, ray_dir, earth_origin,
                    atmosphere_radius);
                float t_max = 0.0;
                if (t_bottom < 0.0)
                {
                    if (t_top < 0.0)
                    {
                        t_max = 0.0;
                    }
                    else
                    {
                        t_max = t_top;
                    }
                }
                else
                {
                    if (t_top > 0.0)
                    {
                        t_max = min(t_top, t_bottom);
                    }
                }
                if (t_max > 0.0)
                {
                    const float kMaxDebugDistance = 9000000.0;
                    float t_max_clamped = min(t_max, kMaxDebugDistance);
                    AddGpuDebugLine(ray_origin,
                        ray_origin + ray_dir * t_max_clamped,
                        float3(1.0, 1.0, 0.0));
                    if (t_max != t_max_clamped)
                    {
                        AddGpuDebugLine(ray_origin,
                            ray_origin + ray_dir * t_max_clamped,
                            float3(1.0, 0.0, 1.0));
                    }
                    t_max = t_max_clamped;

                    const uint kSteps = 64u;
                    const float sample_segment_t = 0.3;
                    float t_prev = 0.0;
                    const float3 debug_offset = float3(0.0, 0.0, -planet_radius);
                    [loop]
                    for (uint i = 0u; i < kSteps; ++i)
                    {
                        float t = t_max * (float(i) + sample_segment_t)
                            / float(kSteps);
                        float3 p_prev = ray_origin_planet + ray_dir * t_prev;
                        float3 p = ray_origin_planet + ray_dir * t;
                        float3 debug_p_prev = debug_offset + p_prev;
                        float3 debug_p = debug_offset + p;
                        if (any(abs(p) > kMaxDebugDistance))
                        {
                            AddGpuDebugLine(ray_origin,
                                ray_origin + ray_dir * 1000.0,
                                float3(1.0, 0.0, 1.0));
                            break;
                        }
                        const bool emit_step = (i == 0u) || ((i % 10u) == 0u);
                        if (emit_step)
                        {
                            AddGpuDebugLine(debug_p_prev, debug_p,
                                float3(0.0, 1.0, 0.0));
                            AddGpuDebugCross(debug_p, float3(0.0, 0.4, 1.0), 0.2);
                        }
                        t_prev = t;
                    }
                }
                else
                {
                    AddGpuDebugLine(mouse_point, center_point,
                        float3(1.0, 0.4, 0.0));
                }
            }
        }
    }

    // Compute linear sky color
    float3 sky_color = ComputeSkyColor(env_data, view_dir);

    return float4(sky_color, 1.0f);
}

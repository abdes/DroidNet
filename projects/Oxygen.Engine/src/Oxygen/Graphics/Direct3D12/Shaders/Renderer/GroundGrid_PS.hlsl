//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/SceneConstants.hlsli"

struct GroundGridPSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct GroundGridPassConstants {
    float4x4 inv_view_proj;
    float4 grid_params0; // plane_height, spacing, major_every, line_thickness
    float4 grid_params1; // major_thickness, axis_thickness, fade_start, fade_end
    float4 grid_params2; // plane_size, origin_x, origin_y, pad
    float4 grid_params3; // fade_power, thickness_max_scale, depth_bias, horizon_boost
    uint depth_srv_index;
    uint flags;
    float2 pad0;
    float4 minor_color;
    float4 major_color;
    float4 axis_color_x;
    float4 axis_color_y;
    float4 origin_color;
};

static inline float GridLineMask(float2 world_xy, float2 spacing, float thickness)
{
    spacing = max(spacing, float2(1e-4, 1e-4));
    const float2 coord = world_xy / spacing;
    const float2 dist_to_line = min(frac(coord), 1.0 - frac(coord));
    const float2 aa = fwidth(coord);
    float2 thickness_coord = max(thickness, 0.0) / spacing;
    thickness_coord = max(thickness_coord, aa);
    const float2 half_width = thickness_coord * 0.5;
    const float2 line_mask = 1.0 - smoothstep(half_width, half_width + aa, dist_to_line);
    return saturate(max(line_mask.x, line_mask.y));
}

static inline float AxisLineMask(float value, float thickness)
{
    const float aa = fwidth(value);
    const float width = max(thickness, aa);
    const float half_width = width * 0.5;
    return saturate(1.0 - smoothstep(half_width, half_width + aa, abs(value)));
}

float4 PS(GroundGridPSInput input) : SV_TARGET
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    ConstantBuffer<GroundGridPassConstants> pass =
        ResourceDescriptorHeap[g_PassConstantsIndex];

    const float plane_height = pass.grid_params0.x;
    const float spacing = pass.grid_params0.y;
    const float major_every = max(pass.grid_params0.z, 1.0);
    const float line_thickness = pass.grid_params0.w;
    const float major_thickness = pass.grid_params1.x;
    const float axis_thickness = pass.grid_params1.y;
    const float fade_start = pass.grid_params1.z;
    const float fade_end = pass.grid_params1.w;
    const float2 grid_origin = pass.grid_params2.yz;
    const float fade_power = pass.grid_params3.x;
    const float thickness_max_scale = pass.grid_params3.y;
    const float depth_bias = pass.grid_params3.z;
    const float horizon_boost = pass.grid_params3.w;

    const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    const float4 clip = float4(ndc, 1.0, 1.0);
    float4 world = mul(pass.inv_view_proj, clip);
    world.xyz /= world.w;

    float3 ray_dir = normalize(world.xyz - camera_position);
    const float denom = ray_dir.z;
    if (abs(denom) < 1e-6) {
        discard;
    }

    const float t = (plane_height - camera_position.z) / denom;
    if (t <= 0.0) {
        discard;
    }

    const float3 world_pos = camera_position + ray_dir * t;

    const float4 clip_pos = mul(projection_matrix, mul(view_matrix, float4(world_pos, 1.0)));
    const float ndc_depth = clip_pos.z / clip_pos.w;
    if (ndc_depth < 0.0 || ndc_depth > 1.0) {
        discard;
    }

    if (pass.depth_srv_index != K_INVALID_BINDLESS_INDEX) {
        Texture2D<float> depth_tex = ResourceDescriptorHeap[pass.depth_srv_index];
        const int2 pixel = int2(input.position.xy);
        const float scene_depth = depth_tex.Load(int3(pixel, 0)).r;
        if (ndc_depth > scene_depth + depth_bias) {
            discard;
        }
    }

    const float2 world_xy = world_pos.xy - grid_origin;
    const float ndotv = abs(ray_dir.z);
    const float thickness_scale = min(1.0 / max(ndotv, 1e-4), thickness_max_scale);

    const float minor_mask = GridLineMask(
        world_xy, float2(spacing, spacing), line_thickness * thickness_scale);
    const float major_mask = GridLineMask(
        world_xy, float2(spacing, spacing) * major_every, major_thickness * thickness_scale);

    const float axis_x_mask = AxisLineMask(
        world_xy.x, axis_thickness * thickness_scale);
    const float axis_y_mask = AxisLineMask(
        world_xy.y, axis_thickness * thickness_scale);
    const float origin_mask = axis_x_mask * axis_y_mask;

    float4 color = pass.minor_color * minor_mask;
    color = lerp(color, pass.major_color, major_mask);
    color = lerp(color, pass.axis_color_x, axis_x_mask);
    color = lerp(color, pass.axis_color_y, axis_y_mask);
    color = lerp(color, pass.origin_color, origin_mask);

    if (fade_end > fade_start) {
        const float dist = length(world_pos.xy - camera_position.xy);
        float fade = saturate((fade_end - dist) / max(fade_end - fade_start, 1e-4));
        fade = pow(fade, max(fade_power, 1e-4));
        color *= fade;
    }

    const float horizon = 1.0 - saturate(abs(ray_dir.z));
    const float horizon_scale = 1.0 + horizon_boost * horizon;
    color *= horizon_scale;
    color.a = saturate(color.a);
    return color;
}

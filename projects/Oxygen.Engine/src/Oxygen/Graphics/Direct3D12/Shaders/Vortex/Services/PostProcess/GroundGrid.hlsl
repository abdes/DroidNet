//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Shared/PristineGrid.hlsli"
#include "Vortex/Shared/Math.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct GroundGridPassConstants
{
    float4x4 inv_view_proj;
    float plane_height;
    float spacing;
    float major_every;
    float fade_start;
    float line_thickness;
    float major_thickness;
    float axis_thickness;
    float fade_power;
    float origin_x;
    float origin_y;
    float horizon_boost;
    float pad_params2_0;
    float grid_offset_x;
    float grid_offset_y;
    uint reserved0;
    uint reserved1;
    float4 minor_color;
    float4 major_color;
    float4 axis_color_x;
    float4 axis_color_y;
    float4 origin_color;
};

struct GroundGridPSOutput
{
    float4 color : SV_TARGET0;
    float depth : SV_Depth;
};

[shader("vertex")]
VortexFullscreenTriangleOutput VortexGroundGridVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
GroundGridPSOutput VortexGroundGridPS(VortexFullscreenTriangleOutput input)
{
    GroundGridPSOutput output;

    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        output.color = float4(0.0, 0.0, 0.0, 0.0);
        output.depth = 0.0;
        return output;
    }

    ConstantBuffer<GroundGridPassConstants> pass = ResourceDescriptorHeap[g_PassConstantsIndex];

    const float plane_height = pass.plane_height;
    const float spacing = max(pass.spacing, EPSILON);
    const float major_every = max(pass.major_every, 1.0);
    const float line_thickness = pass.line_thickness;
    const float major_thickness = pass.major_thickness;
    const float axis_thickness = pass.axis_thickness;
    const float fade_start = pass.fade_start;
    const float2 grid_origin = float2(pass.origin_x, pass.origin_y);
    const float2 grid_offset = float2(pass.grid_offset_x, pass.grid_offset_y);
    const float fade_power = pass.fade_power;
    const float horizon_boost = pass.horizon_boost;

    const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    const float4 clip = float4(ndc, 1.0, 1.0);
    float4 world_rel = mul(pass.inv_view_proj, clip);
    world_rel.xyz /= world_rel.w;
    float3 ray_dir = normalize(world_rel.xyz);

    const float denom = ray_dir.z;
    if (abs(denom) < EPSILON) {
        discard;
    }

    const float rel_plane_z = plane_height - camera_position.z;
    const float t = rel_plane_z / denom;
    if (t <= 0.0) {
        discard;
    }

    const float3 pos_rel = ray_dir * t;
    const float3 pos_abs_approx = camera_position + pos_rel;
    const float4 clip_pos = mul(projection_matrix, mul(view_matrix, float4(pos_abs_approx, 1.0)));
    const float ndc_depth = clip_pos.z / clip_pos.w;
    if (ndc_depth < 0.0 || ndc_depth > 1.0) {
        discard;
    }

    const float2 grid_pos = pos_rel.xy + grid_offset;
    const float2 minor_uv = grid_pos / spacing;
    const float minor_width = line_thickness / spacing;
    const float major_spacing = max(spacing * major_every, EPSILON);
    const float2 major_uv = grid_pos / major_spacing;
    const float major_width = major_thickness / major_spacing;

    const float minor_mask = PristineGrid(minor_uv, minor_width.xx);
    const float major_mask = PristineGrid(major_uv, major_width.xx);
    const float axis_x_mask = PristineLine(pos_abs_approx.y - grid_origin.y, axis_thickness);
    const float axis_y_mask = PristineLine(pos_abs_approx.x - grid_origin.x, axis_thickness);
    const float origin_mask = axis_x_mask * axis_y_mask;

    float4 color = pass.minor_color * minor_mask;
    color = lerp(color, pass.major_color, major_mask);
    color = lerp(color, pass.axis_color_x, axis_x_mask);
    color = lerp(color, pass.axis_color_y, axis_y_mask);
    color = lerp(color, pass.origin_color, origin_mask);

    if (fade_start > 0.0) {
        const float dist = length(pos_rel.xy);
        const float denom_dist = max(dist, fade_start);
        float fade = saturate(fade_start / denom_dist);
        float fade_exp = max(fade_power, EPSILON_LARGE);
        fade = pow(fade, fade_exp);
        color *= fade;
    }

    const float horizon = 1.0 - saturate(abs(ray_dir.z));
    const float horizon_scale = 1.0 + horizon_boost * horizon;
    color *= horizon_scale;

    color.a = saturate(color.a);
    output.color = color;
    output.depth = ndc_depth;
    return output;
}

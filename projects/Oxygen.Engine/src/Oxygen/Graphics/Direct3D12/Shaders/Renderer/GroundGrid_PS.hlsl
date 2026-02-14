//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/SceneConstants.hlsli"
#include "Common/Math.hlsli"

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
    // Registers 0-3 (inv view-projection matrix with NO translation)
    float4x4 inv_view_proj;

    // Register 4 (grid layout)
    float plane_height;
    float spacing;
    float major_every;
    float fade_start;

    // Register 5 (line widths + fade)
    float line_thickness;
    float major_thickness;
    float axis_thickness;
    float fade_power;

    // Register 6 (origin + horizon)
    float origin_x;
    float origin_y;
    float horizon_boost;
    float pad_params2_0;

    // Register 7 (grid offset + SRVs)
    float grid_offset_x;
    float grid_offset_y;
    uint depth_srv_index;
    uint exposure_srv_index;

    // Register 8 (minor color)
    float4 minor_color;
    // Register 9 (major color)
    float4 major_color;
    // Register 10 (axis X color)
    float4 axis_color_x;
    // Register 11 (axis Y color)
    float4 axis_color_y;
    // Register 12 (origin color)
    float4 origin_color;
};

// Visual constants
static const float kGridMinPixelWidthMinor = 1.5;
static const float kGridMinPixelWidthMajor = 2.0;
static const float kAxisMinPixelWidth = 2.0;

// Fade out based on line density to avoid moire
// If spacing < derivative, we are aliasing.
static const float kGridFadeDensityFactor = 3.0;

// Pristine Grid approach-ish: https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
// Modified to match our inputs.
static inline float GridLineMask(float2 uv, float2 spacing, float thickness, float min_pixel_width)
{
    float2 derivative = fwidth(uv);
    // Ensure derivative is not zero to avoid divide by zero
    derivative = max(derivative, float2(EPSILON, EPSILON));

    float2 width = max(thickness, derivative * min_pixel_width);

    // UV is in world units.
    // Calculate distance to nearest line center.
    // Lines are at 0, spacing, 2*spacing...
    // uv % spacing.
    // We want distance from nearest multiple of spacing.

    float2 grid_uv = abs(frac(uv / spacing + 0.5) - 0.5) * spacing;

    // Linear falloff for AA
    float2 draw_width = width;
    float2 line_aa = derivative;

    // Smoothstep for nice AA
    float2 grid = 1.0 - smoothstep(draw_width * 0.5 - line_aa, draw_width * 0.5 + line_aa, grid_uv);

    float mask = saturate(max(grid.x, grid.y));

    // Opacity fade:
    float2 fade_factor = saturate(spacing / (derivative * kGridFadeDensityFactor));
    mask *= min(fade_factor.x, fade_factor.y);

    return mask;
}

static inline float AxisLineMask(float value, float thickness, float min_pixel_width)
{
    float derivative = fwidth(value);
    derivative = max(derivative, EPSILON);
    float width = max(thickness, derivative * min_pixel_width);
    float dist = abs(value);
    float aa = derivative;
    return saturate(1.0 - smoothstep(width * 0.5 - aa, width * 0.5 + aa, dist));
}

float4 PS(GroundGridPSInput input) : SV_TARGET
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    ConstantBuffer<GroundGridPassConstants> pass =
        ResourceDescriptorHeap[g_PassConstantsIndex];

    const float plane_height = pass.plane_height;
    const float spacing = pass.spacing;
    const float major_every = max(pass.major_every, 1.0);
    const float line_thickness = pass.line_thickness;
    const float major_thickness = pass.major_thickness;
    const float axis_thickness = pass.axis_thickness;
    const float fade_start = pass.fade_start;
    const float2 grid_origin = float2(pass.origin_x, pass.origin_y);
    const float2 grid_offset = float2(pass.grid_offset_x, pass.grid_offset_y);
    const float fade_power = pass.fade_power;
    const float horizon_boost = pass.horizon_boost;

    float exposure_mul = max(exposure, EPSILON_SMALL);
    if (pass.exposure_srv_index != K_INVALID_BINDLESS_INDEX) {
        ByteAddressBuffer exposure_buf = ResourceDescriptorHeap[pass.exposure_srv_index];
        exposure_mul = max(asfloat(exposure_buf.Load(4)), EPSILON_SMALL);
    }

    const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    const float4 clip = float4(ndc, 1.0, 1.0);

    // inv_view_proj is now camera-relative (translation removed).
    float4 world_rel = mul(pass.inv_view_proj, clip);
    world_rel.xyz /= world_rel.w; // Relative World Position on the Far Plane

    // Ray direction is simply the vector from (0,0,0) [Relative Camera] to world_rel
    float3 ray_dir = normalize(world_rel.xyz);

    const float denom = ray_dir.z;
    if (abs(denom) < EPSILON) {
        discard;
    }

    // Camera Height from Plane.
    // Plane is at plane_height (Absolute).
    // Camera is at camera_position.z (Absolute).
    // Relative Plane Z = plane_height - camera_position.z
    const float rel_plane_z = plane_height - camera_position.z;
    const float t = rel_plane_z / denom;

    if (t <= 0.0) {
        discard;
    }

    // Relative intersection point
    const float3 pos_rel = ray_dir * t;

    // Absolute Z for scene depth check.
    // We need to reconstruct "true" NDC depth.
    // This is tricky because we don't have the original ViewProj here easily
    // without precision issues if we re-multiply.
    // However, we can construct the position in Clip Space using the SceneConstants ViewProj?
    // SceneConstants ViewProj usually works fine for depth checks.
    // But we are calculating 'pos_rel'.
    // pos_abs = camera_position + pos_rel.
    // This addition is precision-lossy for XY, but Z is usually fine.

    // Standard depth test using SceneConstants logic
    const float3 pos_abs_approx = camera_position + pos_rel;
    const float4 clip_pos = mul(projection_matrix, mul(view_matrix, float4(pos_abs_approx, 1.0)));
    const float ndc_depth = clip_pos.z / clip_pos.w;

    if (ndc_depth < 0.0 || ndc_depth > 1.0) {
        discard;
    }

    if (pass.depth_srv_index != K_INVALID_BINDLESS_INDEX) {
        Texture2D<float> depth_tex = ResourceDescriptorHeap[pass.depth_srv_index];
        const int2 pixel = int2(input.position.xy);
        const float scene_depth = depth_tex.Load(int3(pixel, 0)).r;
        if (ndc_depth > scene_depth) {
            discard;
        }
    }

    // --- Grid Logic (Using Relative Coordinates) ---
    // UV = RelativePosition.xy + GridOffset (Camera % Spacing)
    // This keeps coordinate magnitude small -> High Precision!
    const float2 uv = pos_rel.xy + grid_offset;

    // Minor Grid
    const float minor_mask = GridLineMask(uv, float2(spacing, spacing), line_thickness, kGridMinPixelWidthMinor);

    // Major Grid
    // Major grid aligns with spacing * major_every.
    // We need uv relative to that larger spacing.
    // We can just pass 'uv' but tell the mask the spacing is larger.
    const float major_mask = GridLineMask(uv, float2(spacing, spacing) * major_every, major_thickness, kGridMinPixelWidthMajor);

    // Axis Lines (Need Absolute X/Y approximately)
    // We use pos_abs_approx for this. Since Axis lines are at 0,0,
    // precision only matters when we are near 0,0.
    // When near 0,0, pos_abs_approx is small and precise.
    // When far, it's large and imprecise, but Axis lines are invisible or subpixel anyway.
    const float axis_x_mask = AxisLineMask(pos_abs_approx.x - grid_origin.x, axis_thickness, kAxisMinPixelWidth);
    const float axis_y_mask = AxisLineMask(pos_abs_approx.y - grid_origin.y, axis_thickness, kAxisMinPixelWidth);
    const float origin_mask = axis_x_mask * axis_y_mask;

    // Composition
    float4 color = pass.minor_color * minor_mask;
    color = lerp(color, pass.major_color, major_mask);
    color = lerp(color, pass.axis_color_x, axis_x_mask);
    color = lerp(color, pass.axis_color_y, axis_y_mask);
    color = lerp(color, pass.origin_color, origin_mask);
    color.rgb *= rcp(exposure_mul);

    // Distance Fading (Fog)
    if (fade_start > 0.0) {
        // Distance is just length of relative position (camera to point)
        const float dist = length(pos_rel.xy);
        const float denom = max(dist, fade_start);
        float fade = saturate(fade_start / denom);
        float fade_exp = max(fade_power, EPSILON_LARGE);
        fade = pow(fade, fade_exp);
        color *= fade;
    }

    // Horizon Fade
    const float horizon = 1.0 - saturate(abs(ray_dir.z));
    const float horizon_scale = 1.0 + horizon_boost * horizon;
    color *= horizon_scale;

    color.a = saturate(color.a);
    return color;
}

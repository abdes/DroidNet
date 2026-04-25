//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VolumetricFogOutputHeader
{
    uint output_texture_uav;
    uint output_width;
    uint output_height;
    uint output_depth;
};

struct VolumetricFogGridControl
{
    float start_distance_m;
    float end_distance_m;
    float near_fade_in_distance_m;
    float base_extinction_per_m;
};

struct VolumetricFogMediaControl0
{
    float3 albedo_rgb;
    float scattering_distribution;
};

struct VolumetricFogGridZControl
{
    float3 grid_z_params;
    float shadowed_directional_light0_enabled;
};

struct VolumetricFogMediaControl1
{
    float3 emissive_rgb;
    float static_lighting_scattering_intensity;
};

struct VolumetricFogPassConstants
{
    VolumetricFogOutputHeader output_header;
    VolumetricFogGridControl grid;
    VolumetricFogGridZControl grid_z;
    VolumetricFogMediaControl0 media0;
    VolumetricFogMediaControl1 media1;
    float4 light0_direction_enabled;
    float4 light0_illuminance_rgb;
    float4 light1_direction_enabled;
    float4 light1_illuminance_rgb;
};

static const float kPi = 3.14159265358979323846f;

static float HenyeyGreensteinPhase(float cos_theta, float g)
{
    const float g2 = g * g;
    const float denom = max(1.0f + g2 - 2.0f * g * cos_theta, 1.0e-4f);
    return (1.0f - g2) / (4.0f * kPi * denom * sqrt(denom));
}

static float3 EvaluateDirectionalContribution(
    float4 direction_enabled,
    float4 illuminance_rgb,
    float scattering_distribution,
    float3 view_direction_to_camera,
    float shadow_visibility)
{
    if (direction_enabled.w <= 0.0f) {
        return 0.0f.xxx;
    }

    const float3 light_direction = normalize(direction_enabled.xyz);
    const float phase = HenyeyGreensteinPhase(
        dot(view_direction_to_camera, light_direction),
        scattering_distribution);
    return max(illuminance_rgb.xyz, 0.0f.xxx) * phase * saturate(shadow_visibility);
}

static float ComputeDepthFromZSlice(float z_slice, float3 grid_z_params)
{
    const float scale = max(abs(grid_z_params.x), 1.0e-8f);
    const float distribution = max(abs(grid_z_params.z), 1.0e-4f);
    return (exp2(z_slice / distribution) - grid_z_params.y) / scale;
}

static float ComputeDeviceDepthFromViewDepth(float view_depth)
{
    const float safe_depth = max(view_depth, 1.0e-4f);
    const float4 clip_position =
        mul(projection_matrix, float4(0.0f, 0.0f, -safe_depth, 1.0f));
    return abs(clip_position.w) > 1.0e-6f
        ? clip_position.z / clip_position.w
        : (reverse_z != 0u ? 0.0f : 1.0f);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void VortexVolumetricFogCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    StructuredBuffer<VolumetricFogPassConstants> pass_buffer =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    const VolumetricFogPassConstants pass = pass_buffer[0];
    if (pass.output_header.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_header.output_width
        || dispatch_id.y >= pass.output_header.output_height
        || dispatch_id.z >= pass.output_header.output_depth) {
        return;
    }

    RWTexture3D<float4> output_texture =
        ResourceDescriptorHeap[pass.output_header.output_texture_uav];

    const float slice_distance = clamp(
        ComputeDepthFromZSlice(float(dispatch_id.z) + 0.5f, pass.grid_z.grid_z_params),
        pass.grid.start_distance_m,
        pass.grid.end_distance_m);
    const float ray_length = max(slice_distance - pass.grid.start_distance_m, 0.0f);
    const float near_fade = pass.grid.near_fade_in_distance_m > 1.0e-3f
        ? saturate(ray_length / pass.grid.near_fade_in_distance_m)
        : 1.0f;

    const float extinction =
        max(pass.grid.base_extinction_per_m, 0.0f) * near_fade;
    const float transmittance = exp(-extinction * ray_length);
    const float opacity = saturate(1.0f - transmittance);

    const float2 screen_uv =
        (float2(dispatch_id.xy) + float2(0.5f, 0.5f))
        / max(float2(pass.output_header.output_width, pass.output_header.output_height),
            float2(1.0f, 1.0f));
    const float device_depth = ComputeDeviceDepthFromViewDepth(slice_distance);
    const float3 world_position =
        ReconstructWorldPosition(screen_uv, device_depth, inverse_view_projection_matrix);
    const float3 camera_delta = camera_position - world_position;
    const float camera_delta_length_sq = dot(camera_delta, camera_delta);
    const float3 view_direction_to_camera = camera_delta_length_sq > 1.0e-8f
        ? camera_delta * rsqrt(camera_delta_length_sq)
        : float3(0.0f, 0.0f, 1.0f);

    float light0_shadow_visibility = 1.0f;
    if (pass.grid_z.shadowed_directional_light0_enabled > 0.0f
        && pass.light0_direction_enabled.w > 0.0f) {
        light0_shadow_visibility = ComputeDirectionalVolumetricShadowVisibility(
            world_position, pass.light0_direction_enabled.xyz);
    }

    float3 directional_lighting = 0.0f.xxx;
    directional_lighting += EvaluateDirectionalContribution(
        pass.light0_direction_enabled,
        pass.light0_illuminance_rgb,
        pass.media0.scattering_distribution,
        view_direction_to_camera,
        light0_shadow_visibility);
    directional_lighting += EvaluateDirectionalContribution(
        pass.light1_direction_enabled,
        pass.light1_illuminance_rgb,
        pass.media0.scattering_distribution,
        view_direction_to_camera,
        1.0f);

    const float3 bounded_lighting =
        directional_lighting * (2.0e-5f * pass.media1.static_lighting_scattering_intensity);
    const float3 scattering =
        max(pass.media0.albedo_rgb, 0.0f.xxx) * bounded_lighting
        + max(pass.media1.emissive_rgb, 0.0f.xxx);
    const float3 integrated_luminance = scattering * opacity;

    output_texture[dispatch_id] = float4(max(integrated_luminance, 0.0f.xxx),
        saturate(transmittance));
}

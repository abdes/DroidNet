//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

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

struct VolumetricFogMediaControl1
{
    float3 emissive_rgb;
    float static_lighting_scattering_intensity;
};

struct VolumetricFogPassConstants
{
    VolumetricFogOutputHeader output_header;
    VolumetricFogGridControl grid;
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
    float scattering_distribution)
{
    if (direction_enabled.w <= 0.0f) {
        return 0.0f.xxx;
    }

    const float3 light_direction = normalize(direction_enabled.xyz);
    const float3 representative_view_direction = float3(0.0f, -1.0f, 0.0f);
    const float phase = HenyeyGreensteinPhase(
        dot(representative_view_direction, light_direction),
        scattering_distribution);
    return max(illuminance_rgb.xyz, 0.0f.xxx) * phase;
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

    const float depth_count = max(float(pass.output_header.output_depth), 1.0f);
    const float slice = (float(dispatch_id.z) + 0.5f) / depth_count;
    const float slice_distance = lerp(
        pass.grid.start_distance_m,
        pass.grid.end_distance_m,
        slice * slice);
    const float ray_length = max(slice_distance - pass.grid.start_distance_m, 0.0f);
    const float near_fade = pass.grid.near_fade_in_distance_m > 1.0e-3f
        ? saturate(ray_length / pass.grid.near_fade_in_distance_m)
        : 1.0f;

    const float extinction =
        max(pass.grid.base_extinction_per_m, 0.0f) * near_fade;
    const float transmittance = exp(-extinction * ray_length);
    const float opacity = saturate(1.0f - transmittance);

    float3 directional_lighting = 0.0f.xxx;
    directional_lighting += EvaluateDirectionalContribution(
        pass.light0_direction_enabled,
        pass.light0_illuminance_rgb,
        pass.media0.scattering_distribution);
    directional_lighting += EvaluateDirectionalContribution(
        pass.light1_direction_enabled,
        pass.light1_illuminance_rgb,
        pass.media0.scattering_distribution);

    const float3 bounded_lighting =
        directional_lighting * (2.0e-5f * pass.media1.static_lighting_scattering_intensity);
    const float3 scattering =
        max(pass.media0.albedo_rgb, 0.0f.xxx) * bounded_lighting
        + max(pass.media1.emissive_rgb, 0.0f.xxx);
    const float3 integrated_luminance = scattering * opacity;

    output_texture[dispatch_id] = float4(max(integrated_luminance, 0.0f.xxx),
        saturate(transmittance));
}

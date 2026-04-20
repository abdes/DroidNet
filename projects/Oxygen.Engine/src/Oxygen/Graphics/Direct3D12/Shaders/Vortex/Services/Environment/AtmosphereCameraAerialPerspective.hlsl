//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Shared/ViewConstants.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct AtmosphereCameraAerialPerspectivePassConstants
{
    uint output_texture_uav;
    uint output_width;
    uint output_height;
    uint output_depth;
    uint sky_view_lut_srv;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    float4 sky_aerial_luminance_aerial_start_depth_m;
    float4 aerial_distance_scale_strength_camera_altitude;
    float4 planet_radius_atmosphere_height_height_fog_contribution_pad;
    float4 sun_direction_ws_pad;
    float4 sun_illuminance_rgb_pad;
};

static float3 SafeNormalize(float3 v)
{
    const float len_sq = dot(v, v);
    if (len_sq <= 1.0e-8f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return v * rsqrt(len_sq);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void VortexAtmosphereCameraAerialPerspectiveCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    StructuredBuffer<AtmosphereCameraAerialPerspectivePassConstants> pass_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AtmosphereCameraAerialPerspectivePassConstants pass = pass_buffer[0];
    if (pass.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || pass.sky_view_lut_srv == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_width
        || dispatch_id.y >= pass.output_height
        || dispatch_id.z >= pass.output_depth)
    {
        return;
    }

    RWTexture3D<float4> output_texture = ResourceDescriptorHeap[pass.output_texture_uav];
    Texture2D<float4> sky_view_lut = ResourceDescriptorHeap[pass.sky_view_lut_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    const float3 volume_uv = (float3(dispatch_id) + 0.5f)
        / float3(pass.output_width, pass.output_height, pass.output_depth);
    const float2 sky_uv = float2(volume_uv.x, saturate(1.0f - volume_uv.y * 0.75f));
    const float4 sky_sample = sky_view_lut.SampleLevel(linear_sampler, sky_uv, 0.0f);

    const float start_depth = max(pass.sky_aerial_luminance_aerial_start_depth_m.w, 0.0f);
    const float depth_fraction = volume_uv.z;
    const float aerial_distance_scale = max(
        pass.aerial_distance_scale_strength_camera_altitude.x,
        1.0e-4f);
    const float aerial_strength = max(
        pass.aerial_distance_scale_strength_camera_altitude.y,
        0.0f);
    const float camera_altitude = max(
        pass.aerial_distance_scale_strength_camera_altitude.z,
        0.0f);
    const float atmosphere_height = max(
        pass.planet_radius_atmosphere_height_height_fog_contribution_pad.y,
        1.0f);
    const float height_fog_contribution = max(
        pass.planet_radius_atmosphere_height_height_fog_contribution_pad.z,
        0.0f);

    const float altitude_fraction = saturate(camera_altitude / atmosphere_height);
    const float view_distance_m = start_depth + depth_fraction * aerial_distance_scale * 20000.0f;
    const float extinction = 1.0f - exp(-view_distance_m * 1.0e-4f * aerial_strength);
    const float altitude_fade = exp(-altitude_fraction * 3.0f);
    const float height_fog_mix = saturate(height_fog_contribution * (1.0f - volume_uv.y));
    const float sun_forward = pow(
        saturate(dot(SafeNormalize(pass.sun_direction_ws_pad.xyz), float3(0.0f, -1.0f, 0.0f))),
        8.0f);

    float3 luminance = sky_sample.rgb
        * pass.sky_aerial_luminance_aerial_start_depth_m.xyz
        * altitude_fade
        * extinction;
    luminance += pass.sun_illuminance_rgb_pad.xyz * sun_forward * extinction * 1.0e-5f;
    luminance = lerp(luminance, luminance * 0.8f + sky_sample.rgb * 0.2f, height_fog_mix);

    const float transmittance = saturate(1.0f - extinction);
    output_texture[dispatch_id] = float4(max(luminance, 0.0f.xxx), transmittance);
}

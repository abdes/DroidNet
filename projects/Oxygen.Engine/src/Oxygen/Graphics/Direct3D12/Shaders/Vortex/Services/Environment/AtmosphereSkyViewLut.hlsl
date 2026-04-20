//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Shared/ViewConstants.hlsli"

static const float PI = 3.14159265359f;

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct AtmosphereSkyViewLutPassConstants
{
    uint output_texture_uav;
    uint output_width;
    uint output_height;
    uint _pad0;
    float4 sky_luminance_factor_height_fog_contribution;
    float4 planet_radius_atmosphere_height_camera_altitude_trace_scale;
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

static float3 EvaluateSkyDirection(float2 uv)
{
    const float azimuth = (uv.x * 2.0f - 1.0f) * PI;
    const float elevation = uv.y * (0.5f * PI);
    const float cos_elevation = cos(elevation);
    return SafeNormalize(float3(
        cos_elevation * cos(azimuth),
        cos_elevation * sin(azimuth),
        sin(elevation)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexAtmosphereSkyViewLutCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    StructuredBuffer<AtmosphereSkyViewLutPassConstants> pass_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AtmosphereSkyViewLutPassConstants pass = pass_buffer[0];
    if (pass.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_width
        || dispatch_id.y >= pass.output_height)
    {
        return;
    }

    RWTexture2D<float4> output_texture = ResourceDescriptorHeap[pass.output_texture_uav];

    const float2 uv = (float2(dispatch_id.xy) + 0.5f)
        / float2(pass.output_width, pass.output_height);
    const float3 sky_dir = EvaluateSkyDirection(uv);
    const float3 sun_dir = SafeNormalize(pass.sun_direction_ws_pad.xyz);
    const float sun_amount = saturate(dot(sky_dir, sun_dir));
    const float horizon = saturate(1.0f - abs(sky_dir.z));
    const float zenith = saturate(sky_dir.z * 0.5f + 0.5f);

    const float atmosphere_height = max(
        pass.planet_radius_atmosphere_height_camera_altitude_trace_scale.y,
        1.0f);
    const float camera_altitude = max(
        pass.planet_radius_atmosphere_height_camera_altitude_trace_scale.z,
        0.0f);
    const float altitude_fraction = saturate(camera_altitude / atmosphere_height);
    const float density = exp(-altitude_fraction * 4.0f)
        * lerp(1.2f, 0.35f, zenith);
    const float multi_scatter = 0.2f + 0.8f * saturate(
        pass.planet_radius_atmosphere_height_camera_altitude_trace_scale.w);

    const float3 base_sky = lerp(
        float3(0.18f, 0.28f, 0.48f),
        float3(0.48f, 0.67f, 0.95f),
        zenith);
    const float3 horizon_glow = float3(0.55f, 0.42f, 0.20f)
        * pow(horizon, 1.5f)
        * (0.35f + 0.65f * sun_amount);
    const float3 sun_disk = pass.sun_illuminance_rgb_pad.xyz
        * pow(sun_amount, 64.0f)
        * 2.5e-5f;

    float3 sky_luminance = (base_sky * density + horizon_glow + sun_disk)
        * multi_scatter;
    sky_luminance *= pass.sky_luminance_factor_height_fog_contribution.xyz;

    const float transmittance = saturate(exp(-density * (0.6f + 0.4f * horizon)));
    output_texture[dispatch_id.xy] = float4(max(sky_luminance, 0.0f.xxx), transmittance);
}

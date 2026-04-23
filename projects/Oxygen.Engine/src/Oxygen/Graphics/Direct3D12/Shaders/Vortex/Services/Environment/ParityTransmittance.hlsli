//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_PARITYTRANSMITTANCE_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_PARITYTRANSMITTANCE_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Common/Geometry.hlsli"
// Mirrors the transmittance helpers in UE5.7 SkyAtmosphereCommon.ush and
// SkyAtmosphere.usf.
static float2 ParityGetTransmittanceLutUv(
    float view_height_km,
    float view_zenith_cos_angle,
    float bottom_radius_km,
    float top_radius_km)
{
    // Mirrors UE5.7 SkyAtmosphereCommon.ush::getTransmittanceLutUvs (lines
    // ~178-183): both sqrt arguments are guarded with max(0, ...) to avoid
    // NaNs when numerical drift pushes view_height below bottom_radius or the
    // planet's top shell inversion produces a tiny negative term.
    float H = sqrt(max(0.0f,
        top_radius_km * top_radius_km - bottom_radius_km * bottom_radius_km));
    float rho = sqrt(max(0.0f,
        view_height_km * view_height_km - bottom_radius_km * bottom_radius_km));

    float discriminant = view_height_km * view_height_km
        * (view_zenith_cos_angle * view_zenith_cos_angle - 1.0f)
        + top_radius_km * top_radius_km;
    float d = max(0.0f, (-view_height_km * view_zenith_cos_angle
        + sqrt(max(0.0f, discriminant))));

    float Dmin = top_radius_km - view_height_km;
    float Dmax = rho + H;
    float Xmu = (d - Dmin) / (Dmax - Dmin);
    float Xr = rho / H;
    return float2(Xmu, Xr);
}

// Mirrors UE5.7 SkyAtmosphere.usf::GetTransmittance.
static float3 ParityTransmittanceLutSample(
    uint lut_slot,
    float lut_width,
    float lut_height,
    float cos_zenith,
    float altitude_km,
    float planet_radius_km,
    float atmosphere_height_km)
{
    (void)lut_width;
    (void)lut_height;

    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return 1.0f.xxx;
    }

    Texture2D<float4> lut = ResourceDescriptorHeap[lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const float bottom_radius_km = planet_radius_km;
    const float top_radius_km = planet_radius_km + atmosphere_height_km;
    const float view_height_km = planet_radius_km + altitude_km;
    const float2 uv = ParityGetTransmittanceLutUv(
        view_height_km,
        cos_zenith,
        bottom_radius_km,
        top_radius_km);
    return lut.SampleLevel(linear_sampler, uv, 0.0f).rgb;
}

// Mirrors UE5.7 SkyAtmosphereCommon.ush::GetAtmosphereTransmittance.
static float3 AnalyticalPlanetOccludedTransmittance(
    float3 planet_center_to_world_pos_km,
    float3 world_dir,
    uint lut_slot,
    float lut_width,
    float lut_height,
    float planet_radius_km,
    float atmosphere_height_km)
{
    const float2 hits = RayIntersectSphere(
        planet_center_to_world_pos_km,
        world_dir,
        float4(0.0f, 0.0f, 0.0f, planet_radius_km));
    if (hits.x > 0.0f || hits.y > 0.0f)
    {
        return 0.0f.xxx;
    }

    const float p_height = length(planet_center_to_world_pos_km);
    const float3 up_vector = planet_center_to_world_pos_km / p_height;
    const float light_zenith_cos_angle = dot(world_dir, up_vector);
    const float altitude_km = max(p_height - planet_radius_km, 0.0f);
    return ParityTransmittanceLutSample(
        lut_slot,
        lut_width,
        lut_height,
        light_zenith_cos_angle,
        altitude_km,
        planet_radius_km,
        atmosphere_height_km);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_PARITYTRANSMITTANCE_HLSLI

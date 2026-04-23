//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/EnvironmentStaticData.hlsli"
#include "Vortex/Services/Environment/AtmosphereConstants.hlsli"
#include "Vortex/Services/Environment/AtmosphereMedium.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Math.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct AtmosphereTransmittanceLutPassConstants
{
    uint output_texture_uav;
    uint output_width;
    uint output_height;
    uint integration_sample_count;
    float planet_radius_km;
    float atmosphere_height_km;
    float rayleigh_scale_height_km;
    float mie_scale_height_km;
    float4 rayleigh_scattering_per_km_rgb;
    float4 mie_scattering_per_km_rgb;
    float4 mie_absorption_per_km_rgb;
    float4 ozone_absorption_per_km_rgb;
    float4 ozone_density_layer0;
    float4 ozone_density_layer1;
};

static GpuSkyAtmosphereParams BuildAtmosphereParams(
    AtmosphereTransmittanceLutPassConstants pass)
{
    GpuSkyAtmosphereParams atmo = (GpuSkyAtmosphereParams)0;
    atmo.planet_radius_km = pass.planet_radius_km;
    atmo.atmosphere_height_km = pass.atmosphere_height_km;
    atmo.rayleigh_scattering_per_km_rgb = pass.rayleigh_scattering_per_km_rgb.xyz;
    atmo.rayleigh_scale_height_km = pass.rayleigh_scale_height_km;
    atmo.mie_scattering_per_km_rgb = pass.mie_scattering_per_km_rgb.xyz;
    atmo.mie_scale_height_km = pass.mie_scale_height_km;
    atmo.mie_extinction_per_km_rgb = pass.mie_scattering_per_km_rgb.xyz + pass.mie_absorption_per_km_rgb.xyz;
    atmo.mie_g = 0.8f;
    atmo.absorption_per_km_rgb = pass.ozone_absorption_per_km_rgb.xyz;
    atmo.absorption_density.layers[0].width_km = pass.ozone_density_layer0.x;
    atmo.absorption_density.layers[0].exp_term = pass.ozone_density_layer0.y;
    atmo.absorption_density.layers[0].linear_term = pass.ozone_density_layer0.z;
    atmo.absorption_density.layers[0].constant_term = pass.ozone_density_layer0.w;
    atmo.absorption_density.layers[1].width_km = pass.ozone_density_layer1.x;
    atmo.absorption_density.layers[1].exp_term = pass.ozone_density_layer1.y;
    atmo.absorption_density.layers[1].linear_term = pass.ozone_density_layer1.z;
    atmo.absorption_density.layers[1].constant_term = pass.ozone_density_layer1.w;
    return atmo;
}

static float2 UvToAtmosphereParamsBruneton(
    float2 uv,
    float planet_radius_km,
    float atmosphere_height_km)
{
    const float bottom_radius = planet_radius_km;
    const float top_radius = planet_radius_km + atmosphere_height_km;
    const float H = sqrt(max(0.0, top_radius * top_radius - bottom_radius * bottom_radius));
    float rho = uv.y * H;
    float view_height = sqrt(rho * rho + bottom_radius * bottom_radius);
    float d_min = top_radius - view_height;
    float d_max = rho + H;
    float d = uv.x * (d_max - d_min) + d_min;

    float cos_zenith;
    float denom = 2.0 * d * view_height;
    if (abs(denom) < 1e-6)
    {
        cos_zenith = 1.0;
    }
    else
    {
        cos_zenith = (H * H - rho * rho - d * d) / denom;
    }

    return float2(view_height - bottom_radius, clamp(cos_zenith, -1.0, 1.0));
}

static float3 IntegrateOpticalDepth(
    float3 origin,
    float3 dir,
    float ray_length,
    GpuSkyAtmosphereParams atmo,
    uint integration_sample_count)
{
    float3 optical_depth = 0.0.xxx;
    uint integration_step_count = max(integration_sample_count, 1u);
    float integration_step_size = ray_length / float(integration_step_count);

    [loop]
    for (uint step_index = 0u; step_index < integration_step_count; ++step_index)
    {
        // Mirrors UE5.7 SkyAtmosphere.usf::IntegrateSingleScatteredLuminance
        // uniform-step branch (line ~638):
        //   t = tMax * (SampleI + PixelNoise) / SampleCount;
        // with PixelNoise = DEFAULT_SAMPLE_OFFSET = 0.3f
        // (SkyAtmosphere.usf:403,611). The transmittance LUT pass sets
        // VariableSampleCount = false and PER_PIXEL_NOISE is off in LUT
        // passes, so the constant 0.3f offset applies. The 0.3 bias reduces
        // the systematic under-integration of exponentially-decaying
        // atmosphere density vs. a 0.5 midpoint.
        float ray_distance = (float(step_index) + kSegmentSampleOffset)
            * integration_step_size;
        float3 sample_position = origin + dir * ray_distance;
        float altitude_km = max(length(sample_position) - atmo.planet_radius_km, 0.0f);
        optical_depth.x += AtmosphereExponentialDensity(altitude_km, atmo.rayleigh_scale_height_km) * integration_step_size;
        optical_depth.y += AtmosphereExponentialDensity(altitude_km, atmo.mie_scale_height_km) * integration_step_size;
        optical_depth.z += OzoneAbsorptionDensity(altitude_km, atmo.absorption_density) * integration_step_size;
    }

    return optical_depth;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexAtmosphereTransmittanceLutCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    StructuredBuffer<AtmosphereTransmittanceLutPassConstants> pass_buffer = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AtmosphereTransmittanceLutPassConstants pass = pass_buffer[0];
    if (pass.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_width
        || dispatch_id.y >= pass.output_height)
    {
        return;
    }

    GpuSkyAtmosphereParams atmo = BuildAtmosphereParams(pass);
    RWTexture2D<float4> output_texture = ResourceDescriptorHeap[pass.output_texture_uav];

    float2 uv = (float2(dispatch_id.xy) + 0.5f) / float2(pass.output_width, pass.output_height);
    float2 atmo_params = UvToAtmosphereParamsBruneton(
        uv,
        atmo.planet_radius_km,
        atmo.atmosphere_height_km);
    float altitude_km = atmo_params.x;
    float cos_zenith = atmo_params.y;

    float r = atmo.planet_radius_km + altitude_km;
    float3 origin = float3(0.0f, 0.0f, r);
    float sin_zenith = sqrt(max(0.0f, 1.0f - cos_zenith * cos_zenith));
    float3 dir = float3(sin_zenith, 0.0f, cos_zenith);

    float atmosphere_radius = atmo.planet_radius_km + atmo.atmosphere_height_km;
    float ray_length = RaySphereIntersectNearest(origin, dir, atmosphere_radius);
    float3 optical_depth = 0.0.xxx;

    if (ray_length > 0.0f)
    {
        float ground_dist = RaySphereIntersectNearest(origin, dir, atmo.planet_radius_km);
        float integrate_length = (ground_dist > 0.0f && ground_dist < ray_length) ? ground_dist : ray_length;
        optical_depth = IntegrateOpticalDepth(origin, dir, integrate_length, atmo, pass.integration_sample_count);
    }

    const float3 extinction = (atmo.rayleigh_scattering_per_km_rgb * optical_depth.x)
        + (atmo.mie_extinction_per_km_rgb * optical_depth.y)
        + (atmo.absorption_per_km_rgb * optical_depth.z);
    const float3 transmittance = exp(-extinction);
    output_texture[dispatch_id.xy] = float4(transmittance, 0.0f);
}

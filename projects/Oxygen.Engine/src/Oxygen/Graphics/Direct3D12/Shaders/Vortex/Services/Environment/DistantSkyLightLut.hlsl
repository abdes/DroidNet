//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Atmosphere/IntegrateScatteredLuminance.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Math.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct DistantSkyLightLutPassConstants
{
    uint output_buffer_uav;
    uint transmittance_lut_srv;
    uint multi_scattering_lut_srv;
    uint transmittance_width;
    uint transmittance_height;
    uint multi_scattering_width;
    uint multi_scattering_height;
    uint active_light_count;
    uint integration_sample_count;
    float planet_radius_m;
    float atmosphere_height_m;
    float sample_altitude_km;
    float multi_scattering_factor;
    float rayleigh_scale_height_m;
    float mie_scale_height_m;
    float mie_anisotropy;
    float _pad0;
    float4 light0_direction_ws;
    float4 light1_direction_ws;
    float4 light0_illuminance_rgb;
    float4 light1_illuminance_rgb;
    float4 sky_luminance_factor_rgb;
    float4 ground_albedo_rgb;
    float4 rayleigh_scattering_rgb;
    float4 mie_scattering_rgb;
    float4 mie_absorption_rgb;
    float4 ozone_absorption_rgb;
    float4 ozone_density_layer0;
    float4 ozone_density_layer1;
};

static GpuSkyAtmosphereParams BuildAtmosphereParameters(
    DistantSkyLightLutPassConstants pass_constants)
{
    GpuSkyAtmosphereParams atmosphere_parameters = (GpuSkyAtmosphereParams)0;
    atmosphere_parameters.planet_radius_m = pass_constants.planet_radius_m;
    atmosphere_parameters.atmosphere_height_m = pass_constants.atmosphere_height_m;
    atmosphere_parameters.multi_scattering_factor = pass_constants.multi_scattering_factor;
    atmosphere_parameters.ground_albedo_rgb = pass_constants.ground_albedo_rgb.xyz;
    atmosphere_parameters.rayleigh_scattering_rgb = pass_constants.rayleigh_scattering_rgb.xyz;
    atmosphere_parameters.rayleigh_scale_height_m = pass_constants.rayleigh_scale_height_m;
    atmosphere_parameters.mie_scattering_rgb = pass_constants.mie_scattering_rgb.xyz;
    atmosphere_parameters.mie_scale_height_m = pass_constants.mie_scale_height_m;
    atmosphere_parameters.mie_extinction_rgb = pass_constants.mie_scattering_rgb.xyz
        + pass_constants.mie_absorption_rgb.xyz;
    atmosphere_parameters.mie_g = pass_constants.mie_anisotropy;
    atmosphere_parameters.absorption_rgb = pass_constants.ozone_absorption_rgb.xyz;
    atmosphere_parameters.absorption_density.layers[0].width_m = pass_constants.ozone_density_layer0.x;
    atmosphere_parameters.absorption_density.layers[0].exp_term = pass_constants.ozone_density_layer0.y;
    atmosphere_parameters.absorption_density.layers[0].linear_term = pass_constants.ozone_density_layer0.z;
    atmosphere_parameters.absorption_density.layers[0].constant_term = pass_constants.ozone_density_layer0.w;
    atmosphere_parameters.absorption_density.layers[1].width_m = pass_constants.ozone_density_layer1.x;
    atmosphere_parameters.absorption_density.layers[1].exp_term = pass_constants.ozone_density_layer1.y;
    atmosphere_parameters.absorption_density.layers[1].linear_term = pass_constants.ozone_density_layer1.z;
    atmosphere_parameters.absorption_density.layers[1].constant_term = pass_constants.ozone_density_layer1.w;
    return atmosphere_parameters;
}

static void ComputeUniformSphereDirection(
    uint sample_index,
    uint sample_count,
    out float3 sample_direction)
{
    float phi = TWO_PI * (float(sample_index) + 0.5f) / float(sample_count);
    float cos_theta = 1.0f - 2.0f * (float(sample_index) + 0.5f) / float(sample_count);
    float sin_theta = sqrt(saturate(1.0f - cos_theta * cos_theta));
    sample_direction = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

groupshared float3 GroupSkyLuminanceSamples[64];

static float3 IntegrateSkyLuminanceForAtmosphereLight(
    GpuSkyAtmosphereParams atmosphere_parameters,
    float3 sample_origin,
    float3 integration_direction,
    float3 atmosphere_light_direction,
    float3 atmosphere_light_illuminance,
    uint transmittance_lut_srv,
    float transmittance_lut_width,
    float transmittance_lut_height,
    Texture2D<float4> multi_scattering_lut,
    SamplerState linear_sampler)
{
    float atmosphere_radius = atmosphere_parameters.planet_radius_m
        + atmosphere_parameters.atmosphere_height_m;
    float ray_length = RaySphereIntersectNearest(
        sample_origin,
        integration_direction,
        atmosphere_radius);
    if (ray_length <= 0.0f)
    {
        return 0.0.xxx;
    }

    float ground_distance = RaySphereIntersectNearest(
        sample_origin,
        integration_direction,
        atmosphere_parameters.planet_radius_m);
    if (ground_distance > 0.0f && ground_distance < ray_length)
    {
        ray_length = ground_distance;
    }

    float3 final_transmittance;
    return IntegrateScatteredLuminanceUniform(
        sample_origin,
        integration_direction,
        ray_length,
        16u,
        atmosphere_parameters,
        atmosphere_light_direction,
        atmosphere_light_illuminance,
        transmittance_lut_srv,
        transmittance_lut_width,
        transmittance_lut_height,
        multi_scattering_lut,
        linear_sampler,
        final_transmittance);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexDistantSkyLightLutCS(
    uint3 dispatch_id : SV_DispatchThreadID,
    uint3 group_thread_id : SV_GroupThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    StructuredBuffer<DistantSkyLightLutPassConstants> pass_constant_buffer = ResourceDescriptorHeap[g_PassConstantsIndex];
    const DistantSkyLightLutPassConstants pass_constants = pass_constant_buffer[0];
    if (pass_constants.output_buffer_uav == K_INVALID_BINDLESS_INDEX
        || pass_constants.transmittance_lut_srv == K_INVALID_BINDLESS_INDEX
        || pass_constants.multi_scattering_lut_srv == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    GpuSkyAtmosphereParams atmosphere_parameters = BuildAtmosphereParameters(pass_constants);
    RWStructuredBuffer<float4> output_buffer = ResourceDescriptorHeap[pass_constants.output_buffer_uav];
    Texture2D<float4> multi_scattering_lut = ResourceDescriptorHeap[pass_constants.multi_scattering_lut_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    const uint thread_linear_index = group_thread_id.y * 8u + group_thread_id.x;
    const uint sphere_sample_count = max(pass_constants.integration_sample_count, 64u);
    const float sample_altitude_m = pass_constants.sample_altitude_km * 1000.0f;
    const float3 sample_origin = float3(
        0.0f,
        0.0f,
        atmosphere_parameters.planet_radius_m + sample_altitude_m);

    float3 integration_direction;
    ComputeUniformSphereDirection(thread_linear_index, sphere_sample_count, integration_direction);

    float3 sampled_sky_luminance = 0.0.xxx;
    if (pass_constants.active_light_count > 0u)
    {
        sampled_sky_luminance += IntegrateSkyLuminanceForAtmosphereLight(
            atmosphere_parameters,
            sample_origin,
            integration_direction,
            normalize(pass_constants.light0_direction_ws.xyz),
            pass_constants.light0_illuminance_rgb.xyz,
            pass_constants.transmittance_lut_srv,
            float(pass_constants.transmittance_width),
            float(pass_constants.transmittance_height),
            multi_scattering_lut,
            linear_sampler);
    }
    if (pass_constants.active_light_count > 1u)
    {
        sampled_sky_luminance += IntegrateSkyLuminanceForAtmosphereLight(
            atmosphere_parameters,
            sample_origin,
            integration_direction,
            normalize(pass_constants.light1_direction_ws.xyz),
            pass_constants.light1_illuminance_rgb.xyz,
            pass_constants.transmittance_lut_srv,
            float(pass_constants.transmittance_width),
            float(pass_constants.transmittance_height),
            multi_scattering_lut,
            linear_sampler);
    }

    GroupSkyLuminanceSamples[thread_linear_index]
        = sampled_sky_luminance * pass_constants.sky_luminance_factor_rgb.xyz;
    GroupMemoryBarrierWithGroupSync();

    if (thread_linear_index < 32u)
    {
        GroupSkyLuminanceSamples[thread_linear_index]
            += GroupSkyLuminanceSamples[thread_linear_index + 32u];
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread_linear_index < 16u)
    {
        GroupSkyLuminanceSamples[thread_linear_index]
            += GroupSkyLuminanceSamples[thread_linear_index + 16u];
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread_linear_index < 8u)
    {
        GroupSkyLuminanceSamples[thread_linear_index]
            += GroupSkyLuminanceSamples[thread_linear_index + 8u];
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread_linear_index < 4u)
    {
        GroupSkyLuminanceSamples[thread_linear_index]
            += GroupSkyLuminanceSamples[thread_linear_index + 4u];
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread_linear_index < 2u)
    {
        GroupSkyLuminanceSamples[thread_linear_index]
            += GroupSkyLuminanceSamples[thread_linear_index + 2u];
    }

    if (thread_linear_index == 0u)
    {
        float3 accumulated_sky_luminance = GroupSkyLuminanceSamples[0]
            + GroupSkyLuminanceSamples[1];
        float sphere_sample_solid_angle = FOUR_PI / float(sphere_sample_count);
        float3 accumulated_illuminance = accumulated_sky_luminance * sphere_sample_solid_angle;
        float3 distant_sky_light_luminance = accumulated_illuminance * INV_FOUR_PI;
        output_buffer[0] = float4(max(distant_sky_light_luminance, 0.0f.xxx), 0.0f);
    }
}

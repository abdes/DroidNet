//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Math.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct AtmosphereMultiScatteringLutPassConstants
{
    uint output_texture_uav;
    uint output_width;
    uint output_height;
    uint integration_sample_count;
    uint transmittance_lut_srv;
    uint transmittance_width;
    uint transmittance_height;
    uint active_light_count;
    float planet_radius_m;
    float atmosphere_height_m;
    float rayleigh_scale_height_m;
    float mie_scale_height_m;
    float multi_scattering_factor;
    float mie_anisotropy;
    float _pad1;
    float _pad2;
    float4 ground_albedo_rgb;
    float4 rayleigh_scattering_rgb;
    float4 mie_scattering_rgb;
    float4 mie_absorption_rgb;
    float4 ozone_absorption_rgb;
    float4 ozone_density_layer0;
    float4 ozone_density_layer1;
};

static GpuSkyAtmosphereParams BuildAtmosphereParameters(
    AtmosphereMultiScatteringLutPassConstants pass_constants)
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

static float3 SampleTransmittanceLutDirect(
    uint lut_slot,
    float lut_width,
    float lut_height,
    float cos_zenith,
    float altitude_m,
    GpuSkyAtmosphereParams atmosphere_parameters)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return 1.0f.xxx;
    }

    const float2 uv = ApplyHalfTexelOffset(
        GetTransmittanceLutUv(
            cos_zenith,
            altitude_m,
            atmosphere_parameters.planet_radius_m,
            atmosphere_parameters.atmosphere_height_m),
        lut_width,
        lut_height);
    Texture2D<float4> lut = ResourceDescriptorHeap[lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    return lut.SampleLevel(linear_sampler, uv, 0.0f).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexAtmosphereMultiScatteringLutCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    StructuredBuffer<AtmosphereMultiScatteringLutPassConstants> pass_constant_buffer = ResourceDescriptorHeap[g_PassConstantsIndex];
    const AtmosphereMultiScatteringLutPassConstants pass_constants = pass_constant_buffer[0];
    if (pass_constants.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || pass_constants.transmittance_lut_srv == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass_constants.output_width
        || dispatch_id.y >= pass_constants.output_height)
    {
        return;
    }

    GpuSkyAtmosphereParams atmosphere_parameters = BuildAtmosphereParameters(pass_constants);
    RWTexture2D<float4> output_texture = ResourceDescriptorHeap[pass_constants.output_texture_uav];

    float2 texel_center_uv = (float2(dispatch_id.xy) + 0.5f)
        / float2(pass_constants.output_width, pass_constants.output_height);

    float cosine_light_zenith = texel_center_uv.x * 2.0f - 1.0f;
    float3 light_direction = float3(
        0.0f,
        sqrt(saturate(1.0f - cosine_light_zenith * cosine_light_zenith)),
        cosine_light_zenith);
    float view_height = atmosphere_parameters.planet_radius_m
        + texel_center_uv.y * atmosphere_parameters.atmosphere_height_m;

    float3 sample_origin = float3(0.0f, 0.0f, view_height);
    uint sphere_sample_count = max(pass_constants.integration_sample_count, 1u);
    const uint raymarch_step_count = 16u;
    float3 integrated_multi_scattering = 0.0.xxx;
    float3 multi_scattering_transfer = 0.0.xxx;

    [loop]
    for (uint sphere_sample_index = 0u; sphere_sample_index < sphere_sample_count; ++sphere_sample_index)
    {
        float phi = TWO_PI * (float(sphere_sample_index) + 0.5f) / float(sphere_sample_count);
        float cos_theta = 1.0f - 2.0f * (float(sphere_sample_index) + 0.5f) / float(sphere_sample_count);
        float sin_theta = sqrt(saturate(1.0f - cos_theta * cos_theta));
        float3 integration_direction = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

        float atmosphere_radius = atmosphere_parameters.planet_radius_m
            + atmosphere_parameters.atmosphere_height_m;
        float ray_length = RaySphereIntersectNearest(sample_origin, integration_direction, atmosphere_radius);
        float ground_distance = RaySphereIntersectNearest(sample_origin, integration_direction, atmosphere_parameters.planet_radius_m);
        if (ground_distance > 0.0f && (ray_length < 0.0f || ground_distance < ray_length))
        {
            ray_length = ground_distance;
        }

        if (ray_length <= 0.0f)
        {
            continue;
        }

        float3 integrated_in_scattered_luminance = 0.0.xxx;
        float3 accumulated_optical_depth = 0.0.xxx;
        float raymarch_step_size = ray_length / float(raymarch_step_count);

        [loop]
        for (uint raymarch_step_index = 0u; raymarch_step_index < raymarch_step_count; ++raymarch_step_index)
        {
            float3 sample_position = sample_origin
                + integration_direction * ((float(raymarch_step_index) + 0.5f) * raymarch_step_size);
            float altitude_m = max(length(sample_position) - atmosphere_parameters.planet_radius_m, 0.0f);

            float rayleigh_density = AtmosphereExponentialDensity(
                altitude_m,
                atmosphere_parameters.rayleigh_scale_height_m);
            float mie_density = AtmosphereExponentialDensity(
                altitude_m,
                atmosphere_parameters.mie_scale_height_m);
            float absorption_density = OzoneAbsorptionDensity(
                altitude_m,
                atmosphere_parameters.absorption_density);

            float3 optical_depth_step = float3(
                rayleigh_density,
                mie_density,
                absorption_density) * raymarch_step_size;
            float3 view_transmittance = TransmittanceFromOpticalDepth(
                accumulated_optical_depth + optical_depth_step * 0.5f,
                atmosphere_parameters);

            float3 sample_up_direction = normalize(sample_position);
            float cosine_sun_zenith = dot(sample_up_direction, light_direction);
            float3 sun_transmittance = SampleTransmittanceLutDirect(
                pass_constants.transmittance_lut_srv,
                float(pass_constants.transmittance_width),
                float(pass_constants.transmittance_height),
                cosine_sun_zenith,
                altitude_m,
                atmosphere_parameters);

            float3 scattering_extinction = atmosphere_parameters.rayleigh_scattering_rgb * rayleigh_density
                + atmosphere_parameters.mie_scattering_rgb * mie_density;
            integrated_in_scattered_luminance += sun_transmittance
                * view_transmittance
                * scattering_extinction
                * raymarch_step_size;
            accumulated_optical_depth += optical_depth_step;
        }

        if (ground_distance > 0.0f && (ray_length < 0.0f || ground_distance <= ray_length))
        {
            float3 ground_position = sample_origin + integration_direction * ground_distance;
            float3 ground_normal = normalize(ground_position);
            float ground_light_cosine = max(0.0f, dot(ground_normal, light_direction));
            float3 ground_sun_transmittance = SampleTransmittanceLutDirect(
                pass_constants.transmittance_lut_srv,
                float(pass_constants.transmittance_width),
                float(pass_constants.transmittance_height),
                ground_light_cosine,
                0.0f,
                atmosphere_parameters);
            float3 ground_view_transmittance = TransmittanceFromOpticalDepth(
                accumulated_optical_depth,
                atmosphere_parameters);
            integrated_in_scattered_luminance += (atmosphere_parameters.ground_albedo_rgb * INV_PI)
                * ground_light_cosine
                * ground_sun_transmittance
                * ground_view_transmittance;
        }

        integrated_multi_scattering += integrated_in_scattered_luminance * INV_FOUR_PI;
        multi_scattering_transfer += (1.0f - TransmittanceFromOpticalDepth(
            accumulated_optical_depth,
            atmosphere_parameters)) * INV_FOUR_PI;
    }

    integrated_multi_scattering /= float(sphere_sample_count);
    multi_scattering_transfer /= float(sphere_sample_count);

    float3 multi_scattering_geometric_ratio = multi_scattering_transfer;
    float3 multi_scattered_luminance = integrated_multi_scattering
        * (1.0f / max(1.0f.xxx - multi_scattering_geometric_ratio, 1.0e-4f.xxx));
    multi_scattered_luminance *= atmosphere_parameters.multi_scattering_factor;

    output_texture[dispatch_id.xy] = float4(
        max(multi_scattered_luminance, 0.0f.xxx),
        dot(multi_scattering_transfer, 1.0f.xxx / 3.0f));
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"
#include "Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli"
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
    // This path does not sample the multi-scattering LUT while building it
    // (`multi_scattering_approx_sampling_enabled = false` below), but the
    // shared integrator signature still carries a Texture2D parameter.
    // Bind a real SRV, never the output UAV index. Using the UAV bindless slot
    // here is an invalid descriptor-type mismatch and can trigger device removal.
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[pass_constants.transmittance_lut_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    float2 texel_center_uv = (float2(dispatch_id.xy) + 0.5f)
        / float2(pass_constants.output_width, pass_constants.output_height);

    float cosine_light_zenith = texel_center_uv.x * 2.0f - 1.0f;
    float3 light_direction = float3(
        0.0f,
        sqrt(saturate(1.0f - cosine_light_zenith * cosine_light_zenith)),
        cosine_light_zenith);
    float view_height = atmosphere_parameters.planet_radius_m
        + texel_center_uv.y * atmosphere_parameters.atmosphere_height_m;

    const float3 sample_origin = float3(0.0f, 0.0f, view_height);
    const float3 world_dir = float3(0.0f, 0.0f, 1.0f);
    const float3 null_light_direction = float3(0.0f, 0.0f, 1.0f);
    const float3 null_light_illuminance = 0.0f.xxx;
    const float3 one_illuminance = 1.0f.xxx;
    const float sphere_solid_angle = 4.0f * PI;
    const float isotropic_phase = 1.0f / sphere_solid_angle;
    VortexSamplingSetup sampling = (VortexSamplingSetup)0;
    sampling.VariableSampleCount = false;
    sampling.SampleCountIni = max(1.0f, (float)pass_constants.integration_sample_count);
    sampling.MinSampleCount = 1.0f;
    sampling.MaxSampleCount = 1.0f;
    sampling.DistanceToSampleCountMaxInv = 0.0f;

    const VortexSingleScatteringResult r0 = VortexIntegrateSingleScatteredLuminance(
        0.0f.xx,
        sample_origin,
        world_dir,
        VortexResolveFarDepthReference(),
        true,
        sampling,
        false,
        false,
        light_direction,
        null_light_direction,
        one_illuminance,
        null_light_illuminance,
        1.0f,
        1.0f,
        atmosphere_parameters,
        pass_constants.transmittance_lut_srv,
        (float)pass_constants.transmittance_width,
        (float)pass_constants.transmittance_height,
        multi_scat_lut,
        linear_sampler,
        9000000.0f);
    const VortexSingleScatteringResult r1 = VortexIntegrateSingleScatteredLuminance(
        0.0f.xx,
        sample_origin,
        -world_dir,
        VortexResolveFarDepthReference(),
        true,
        sampling,
        false,
        false,
        light_direction,
        null_light_direction,
        one_illuminance,
        null_light_illuminance,
        1.0f,
        1.0f,
        atmosphere_parameters,
        pass_constants.transmittance_lut_srv,
        (float)pass_constants.transmittance_width,
        (float)pass_constants.transmittance_height,
        multi_scat_lut,
        linear_sampler,
        9000000.0f);

    const float3 integrated_illuminance = (sphere_solid_angle * 0.5f) * (r0.L + r1.L);
    const float3 multi_scat_as1 = 0.5f * (r0.MultiScatAs1 + r1.MultiScatAs1);
    const float3 in_scattered_luminance = integrated_illuminance * isotropic_phase;
    const float3 multi_scat_as1_sqr = multi_scat_as1 * multi_scat_as1;
    float3 multi_scattered_luminance = in_scattered_luminance
        * (1.0f.xxx + multi_scat_as1
            + multi_scat_as1_sqr
            + multi_scat_as1 * multi_scat_as1_sqr
            + multi_scat_as1_sqr * multi_scat_as1_sqr);
    multi_scattered_luminance *= atmosphere_parameters.multi_scattering_factor;

    output_texture[dispatch_id.xy] = float4(
        max(multi_scattered_luminance, 0.0f.xxx),
        0.0f);
}

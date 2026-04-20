//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli"
#include "Vortex/Shared/ViewConstants.hlsli"
#include "Renderer/ViewColorData.hlsli"
#include "Renderer/ViewFrameBindings.hlsli"

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
    uint transmittance_lut_srv;
    uint multi_scattering_lut_srv;
    uint transmittance_width;
    uint transmittance_height;
    uint multi_scattering_width;
    uint multi_scattering_height;
    uint active_light_count;
    uint _pad0;
    float sample_count_min;
    float sample_count_max;
    float distance_to_sample_count_max_inv;
    float planet_radius_m;
    float atmosphere_height_m;
    float camera_altitude_m;
    float rayleigh_scale_height_m;
    float mie_scale_height_m;
    float multi_scattering_factor;
    float mie_anisotropy;
    float4 ground_albedo_rgb;
    float4 rayleigh_scattering_rgb;
    float4 mie_scattering_rgb;
    float4 mie_absorption_rgb;
    float4 ozone_absorption_rgb;
    float4 ozone_density_layer0;
    float4 ozone_density_layer1;
    float4 sky_view_lut_referential_row0;
    float4 sky_view_lut_referential_row1;
    float4 sky_view_lut_referential_row2;
    float4 sky_luminance_factor_rgb;
    float4 sky_and_aerial_luminance_factor_rgb;
    float4 light0_direction_enabled;
    float4 light0_illuminance_rgb;
    float4 light1_direction_enabled;
    float4 light1_illuminance_rgb;
};

static GpuSkyAtmosphereParams BuildAtmosphereParams(
    AtmosphereSkyViewLutPassConstants pass)
{
    AtmosphereDensityProfile ozone_density = (AtmosphereDensityProfile)0;
    ozone_density.layers[0].width_m = pass.ozone_density_layer0.x;
    ozone_density.layers[0].exp_term = pass.ozone_density_layer0.y;
    ozone_density.layers[0].linear_term = pass.ozone_density_layer0.z;
    ozone_density.layers[0].constant_term = pass.ozone_density_layer0.w;
    ozone_density.layers[1].width_m = pass.ozone_density_layer1.x;
    ozone_density.layers[1].exp_term = pass.ozone_density_layer1.y;
    ozone_density.layers[1].linear_term = pass.ozone_density_layer1.z;
    ozone_density.layers[1].constant_term = pass.ozone_density_layer1.w;

    return BuildVortexAtmosphereParams(
        pass.planet_radius_m,
        pass.atmosphere_height_m,
        pass.multi_scattering_factor,
        1.0f,
        pass.rayleigh_scale_height_m,
        pass.mie_scale_height_m,
        pass.mie_anisotropy,
        pass.ground_albedo_rgb.xyz,
        0.0f,
        pass.rayleigh_scattering_rgb.xyz,
        pass.mie_scattering_rgb.xyz,
        pass.mie_absorption_rgb.xyz,
        pass.ozone_absorption_rgb.xyz,
        ozone_density,
        0u,
        pass.transmittance_lut_srv,
        (float)pass.transmittance_width,
        (float)pass.transmittance_height,
        pass.multi_scattering_lut_srv);
}

static float GetVortexExposure()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    if (view_bindings.view_color_frame_slot != K_INVALID_BINDLESS_INDEX)
    {
        const ViewColorData view_color =
            LoadViewColorData(view_bindings.view_color_frame_slot);
        return max(view_color.exposure, 0.0f);
    }
    return 1.0f;
}

static VortexSingleScatteringResult IntegrateSkyLight(
    GpuSkyAtmosphereParams atmosphere,
    AtmosphereSkyViewLutPassConstants pass,
    float3 ray_origin,
    float3 ray_direction,
    float ray_length)
{
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[pass.multi_scattering_lut_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const float output_pre_exposure = max(GetVortexExposure(), 1.0e-6f);
    const float3x3 local_referential = float3x3(
        pass.sky_view_lut_referential_row0.xyz,
        pass.sky_view_lut_referential_row1.xyz,
        pass.sky_view_lut_referential_row2.xyz);
    VortexSamplingSetup sampling = (VortexSamplingSetup)0;
    sampling.VariableSampleCount = true;
    sampling.SampleCountIni = 0.0f;
    sampling.MinSampleCount = max(pass.sample_count_min, 1.0f);
    sampling.MaxSampleCount = max(pass.sample_count_max, pass.sample_count_min);
    sampling.DistanceToSampleCountMaxInv = pass.distance_to_sample_count_max_inv;
    return VortexIntegrateSingleScatteredLuminance(
        ray_origin,
        ray_direction,
        false,
        sampling,
        true,
        true,
        pass.light0_direction_enabled.w > 0.5f
            ? VortexSafeNormalize(mul(local_referential, pass.light0_direction_enabled.xyz))
            : float3(0.0f, 0.0f, 1.0f),
        pass.light1_direction_enabled.w > 0.5f
            ? VortexSafeNormalize(mul(local_referential, pass.light1_direction_enabled.xyz))
            : float3(0.0f, 0.0f, 1.0f),
        pass.light0_direction_enabled.w > 0.5f ? pass.light0_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz : 0.0f.xxx,
        pass.light1_direction_enabled.w > 0.5f ? pass.light1_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz : 0.0f.xxx,
        output_pre_exposure,
        1.0f,
        atmosphere,
        pass.transmittance_lut_srv,
        (float)pass.transmittance_width,
        (float)pass.transmittance_height,
        multi_scat_lut,
        linear_sampler,
        ray_length);
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
        || pass.transmittance_lut_srv == K_INVALID_BINDLESS_INDEX
        || pass.multi_scattering_lut_srv == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_width
        || dispatch_id.y >= pass.output_height)
    {
        return;
    }

    RWTexture2D<float4> output_texture = ResourceDescriptorHeap[pass.output_texture_uav];

    const GpuSkyAtmosphereParams atmosphere = BuildAtmosphereParams(pass);
    const float2 lut_size = float2(pass.output_width, pass.output_height);
    const float2 lut_inv_size = 1.0f.xx / lut_size;
    float2 uv = (float2(dispatch_id.xy) + 0.5f) / lut_size;
    uv = saturate(VortexFromSubUvsToUnit(uv, lut_size, lut_inv_size));

    const float view_height = atmosphere.planet_radius_m + max(pass.camera_altitude_m, 0.0f);
    float3 ray_origin = float3(0.0f, 0.0f, view_height);
    float3 ray_direction = 0.0f.xxx;
    UvToSkyViewLutParams(ray_direction, view_height, atmosphere.planet_radius_m, uv);
    ray_direction = VortexSafeNormalize(ray_direction);

    const float atmosphere_radius = atmosphere.planet_radius_m + atmosphere.atmosphere_height_m;
    if (!MoveToTopAtmosphere(ray_origin, ray_direction, atmosphere_radius))
    {
        output_texture[dispatch_id.xy] = 0.0f.xxxx;
        return;
    }

    const VortexSingleScatteringResult scattering = IntegrateSkyLight(
        atmosphere,
        pass,
        ray_origin,
        ray_direction,
        9000000.0f);
    const float3 luminance = max(scattering.L, 0.0f.xxx);
    const float3 throughput = scattering.Transmittance;
    const float transmittance = dot(throughput, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));
    output_texture[dispatch_id.xy] = float4(luminance, saturate(transmittance));
}

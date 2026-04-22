//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli"
#include "Renderer/ViewColorData.hlsli"
#include "Renderer/ViewFrameBindings.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct SkyViewOutputHeader
{
    uint output_texture_uav;
    uint output_width;
    uint output_height;
    uint transmittance_lut_srv;
};

struct SkyViewLutHeader
{
    uint multi_scattering_lut_srv;
    uint transmittance_width;
    uint transmittance_height;
    uint multi_scattering_width;
};

struct SkyViewDispatchHeader
{
    uint multi_scattering_height;
    uint active_light_count;
    uint _pad0;
    uint _pad1;
};

struct SkyViewSamplingAtmosphere0
{
    float sample_count_min;
    float sample_count_max;
    float distance_to_sample_count_max_inv;
    float planet_radius_km;
};

struct SkyViewSamplingAtmosphere1
{
    float atmosphere_height_km;
    float camera_altitude_km;
    float rayleigh_scale_height_km;
    float mie_scale_height_km;
};

struct SkyViewPhaseFactors0
{
    float multi_scattering_factor;
    float mie_anisotropy;
    float _pad0;
    float _pad1;
};

struct AtmosphereSkyViewLutPassConstants
{
    SkyViewOutputHeader output_header;
    SkyViewLutHeader lut_header;
    SkyViewDispatchHeader dispatch_header;
    SkyViewSamplingAtmosphere0 sampling_atmosphere0;
    SkyViewSamplingAtmosphere1 sampling_atmosphere1;
    SkyViewPhaseFactors0 phase_factors0;
    float4 ground_albedo_rgb;
    float4 rayleigh_scattering_per_km_rgb;
    float4 mie_scattering_per_km_rgb;
    float4 mie_absorption_per_km_rgb;
    float4 ozone_absorption_per_km_rgb;
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

static uint PassOutputTextureUav(AtmosphereSkyViewLutPassConstants pass) { return pass.output_header.output_texture_uav; }
static uint PassOutputWidth(AtmosphereSkyViewLutPassConstants pass) { return pass.output_header.output_width; }
static uint PassOutputHeight(AtmosphereSkyViewLutPassConstants pass) { return pass.output_header.output_height; }
static uint PassTransmittanceLutSrv(AtmosphereSkyViewLutPassConstants pass) { return pass.output_header.transmittance_lut_srv; }
static uint PassMultiScatteringLutSrv(AtmosphereSkyViewLutPassConstants pass) { return pass.lut_header.multi_scattering_lut_srv; }
static uint PassTransmittanceWidth(AtmosphereSkyViewLutPassConstants pass) { return pass.lut_header.transmittance_width; }
static uint PassTransmittanceHeight(AtmosphereSkyViewLutPassConstants pass) { return pass.lut_header.transmittance_height; }
static uint PassMultiScatteringWidth(AtmosphereSkyViewLutPassConstants pass) { return pass.lut_header.multi_scattering_width; }
static uint PassMultiScatteringHeight(AtmosphereSkyViewLutPassConstants pass) { return pass.dispatch_header.multi_scattering_height; }
static uint PassActiveLightCount(AtmosphereSkyViewLutPassConstants pass) { return pass.dispatch_header.active_light_count; }
static float PassSampleCountMin(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere0.sample_count_min; }
static float PassSampleCountMax(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere0.sample_count_max; }
static float PassDistanceToSampleCountMaxInv(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere0.distance_to_sample_count_max_inv; }
static float PassPlanetRadiusKm(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere0.planet_radius_km; }
static float PassAtmosphereHeightKm(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere1.atmosphere_height_km; }
static float PassCameraAltitudeKm(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere1.camera_altitude_km; }
static float PassRayleighScaleHeightKm(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere1.rayleigh_scale_height_km; }
static float PassMieScaleHeightKm(AtmosphereSkyViewLutPassConstants pass) { return pass.sampling_atmosphere1.mie_scale_height_km; }
static float PassMultiScatteringFactor(AtmosphereSkyViewLutPassConstants pass) { return pass.phase_factors0.multi_scattering_factor; }
static float PassMieAnisotropy(AtmosphereSkyViewLutPassConstants pass) { return pass.phase_factors0.mie_anisotropy; }

static GpuSkyAtmosphereParams BuildAtmosphereParams(
    AtmosphereSkyViewLutPassConstants pass)
{
    AtmosphereDensityProfile ozone_density = (AtmosphereDensityProfile)0;
    ozone_density.layers[0].width_km = pass.ozone_density_layer0.x;
    ozone_density.layers[0].exp_term = pass.ozone_density_layer0.y;
    ozone_density.layers[0].linear_term = pass.ozone_density_layer0.z;
    ozone_density.layers[0].constant_term = pass.ozone_density_layer0.w;
    ozone_density.layers[1].width_km = pass.ozone_density_layer1.x;
    ozone_density.layers[1].exp_term = pass.ozone_density_layer1.y;
    ozone_density.layers[1].linear_term = pass.ozone_density_layer1.z;
    ozone_density.layers[1].constant_term = pass.ozone_density_layer1.w;

    return BuildVortexAtmosphereParams(
        PassPlanetRadiusKm(pass),
        PassAtmosphereHeightKm(pass),
        PassMultiScatteringFactor(pass),
        1.0f,
        PassRayleighScaleHeightKm(pass),
        PassMieScaleHeightKm(pass),
        PassMieAnisotropy(pass),
        pass.ground_albedo_rgb.xyz,
        0.0f,
        pass.rayleigh_scattering_per_km_rgb.xyz,
        pass.mie_scattering_per_km_rgb.xyz,
        pass.mie_absorption_per_km_rgb.xyz,
        pass.ozone_absorption_per_km_rgb.xyz,
        ozone_density,
        0u,
        PassTransmittanceLutSrv(pass),
        (float)PassTransmittanceWidth(pass),
        (float)PassTransmittanceHeight(pass),
        PassMultiScatteringLutSrv(pass));
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

static float3 ApplyPassSkyViewReferential(
    AtmosphereSkyViewLutPassConstants pass,
    float3 world_direction)
{
    // Pass constants carry the same shared local frame published in
    // EnvironmentViewData:
    //   local +X = forward, local +Y = right, local +Z = up.
    // Both LUT generation and main-view sampling must use this exact basis.
    return float3(
        dot(pass.sky_view_lut_referential_row0.xyz, world_direction),
        dot(pass.sky_view_lut_referential_row1.xyz, world_direction),
        dot(pass.sky_view_lut_referential_row2.xyz, world_direction));
}

static VortexSingleScatteringResult IntegrateSkyLight(
    GpuSkyAtmosphereParams atmosphere,
    AtmosphereSkyViewLutPassConstants pass,
    float3 ray_origin,
    float3 ray_direction,
    float ray_length)
{
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[PassMultiScatteringLutSrv(pass)];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const float output_pre_exposure = max(GetVortexExposure(), 1.0e-6f);
    VortexSamplingSetup sampling = (VortexSamplingSetup)0;
    sampling.VariableSampleCount = true;
    sampling.SampleCountIni = 0.0f;
    sampling.MinSampleCount = max(PassSampleCountMin(pass), 1.0f);
    sampling.MaxSampleCount = max(PassSampleCountMax(pass), PassSampleCountMin(pass));
    sampling.DistanceToSampleCountMaxInv = PassDistanceToSampleCountMaxInv(pass);
    return VortexIntegrateSingleScatteredLuminance(
        0.0f.xx,
        ray_origin,
        ray_direction,
        VortexResolveFarDepthReference(),
        false,
        sampling,
        true,
        true,
        pass.light0_direction_enabled.w > 0.5f
            ? VortexSafeNormalize(ApplyPassSkyViewReferential(pass, pass.light0_direction_enabled.xyz))
            : float3(0.0f, 0.0f, 1.0f),
        pass.light1_direction_enabled.w > 0.5f
            ? VortexSafeNormalize(ApplyPassSkyViewReferential(pass, pass.light1_direction_enabled.xyz))
            : float3(0.0f, 0.0f, 1.0f),
        pass.light0_direction_enabled.w > 0.5f ? pass.light0_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz : 0.0f.xxx,
        pass.light1_direction_enabled.w > 0.5f ? pass.light1_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz : 0.0f.xxx,
        output_pre_exposure,
        1.0f,
        atmosphere,
        PassTransmittanceLutSrv(pass),
        (float)PassTransmittanceWidth(pass),
        (float)PassTransmittanceHeight(pass),
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
    const uint output_texture_uav = PassOutputTextureUav(pass);
    const uint output_width = PassOutputWidth(pass);
    const uint output_height = PassOutputHeight(pass);
    const uint transmittance_lut_srv = PassTransmittanceLutSrv(pass);
    if (output_texture_uav == K_INVALID_BINDLESS_INDEX
        || transmittance_lut_srv == K_INVALID_BINDLESS_INDEX
        || PassMultiScatteringLutSrv(pass) == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= output_width
        || dispatch_id.y >= output_height)
    {
        return;
    }

    RWTexture2D<float4> output_texture = ResourceDescriptorHeap[output_texture_uav];
    const GpuSkyAtmosphereParams atmosphere = BuildAtmosphereParams(pass);
    const float2 lut_size = float2(output_width, output_height);
    const float2 lut_inv_size = 1.0f.xx / lut_size;
    float2 uv = (float2(dispatch_id.xy) + 0.5f) / lut_size;
    uv = saturate(VortexFromSubUvsToUnit(uv, lut_size, lut_inv_size));

    const float view_height = PassPlanetRadiusKm(pass) + max(PassCameraAltitudeKm(pass), 0.0f);
    float3 ray_origin = float3(0.0f, 0.0f, view_height);
    float3 ray_direction = 0.0f.xxx;
    UvToSkyViewLutParams(ray_direction, view_height, PassPlanetRadiusKm(pass), uv);
    ray_direction = VortexSafeNormalize(ray_direction);

    const float atmosphere_radius = PassPlanetRadiusKm(pass) + PassAtmosphereHeightKm(pass);
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
        9000.0f);
    const float transmittance = dot(
        scattering.Transmittance,
        float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));
    output_texture[dispatch_id.xy] = float4(
        max(scattering.L, 0.0f.xxx),
        saturate(transmittance));
}

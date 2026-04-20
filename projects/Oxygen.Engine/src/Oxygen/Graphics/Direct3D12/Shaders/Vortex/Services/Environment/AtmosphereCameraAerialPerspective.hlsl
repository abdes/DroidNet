//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"
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
    uint transmittance_lut_srv;
    uint multi_scattering_lut_srv;
    uint transmittance_width;
    uint transmittance_height;
    uint multi_scattering_width;
    uint multi_scattering_height;
    uint active_light_count;
    uint depth_resolution;
    float depth_resolution_inv;
    float depth_slice_length_km;
    float depth_slice_length_km_inv;
    float start_depth_km;
    float planet_radius_m;
    float atmosphere_height_m;
    float aerial_perspective_distance_scale;
    float aerial_scattering_strength;
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
    float4 camera_planet_position;
    float4 sky_and_aerial_luminance_factor_rgb;
    float4 light0_direction_enabled;
    float4 light0_illuminance_rgb;
    float4 light1_direction_enabled;
    float4 light1_illuminance_rgb;
};

static GpuSkyAtmosphereParams BuildAtmosphereParams(
    AtmosphereCameraAerialPerspectivePassConstants pass)
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
        pass.rayleigh_scale_height_m,
        pass.mie_scale_height_m,
        pass.mie_anisotropy,
        pass.ground_albedo_rgb.xyz,
        pass.rayleigh_scattering_rgb.xyz,
        pass.mie_scattering_rgb.xyz,
        pass.mie_absorption_rgb.xyz,
        pass.ozone_absorption_rgb.xyz,
        ozone_density,
        pass.transmittance_lut_srv,
        (float)pass.transmittance_width,
        (float)pass.transmittance_height,
        pass.multi_scattering_lut_srv);
}

static float ResolveFarDepthReference()
{
    return projection_matrix._33 > 0.0f ? 0.0f : 1.0f;
}

static float3 IntegrateCameraAerialLight(
    GpuSkyAtmosphereParams atmosphere,
    AtmosphereCameraAerialPerspectivePassConstants pass,
    float3 ray_origin,
    float3 ray_direction,
    float ray_length,
    float3 light_direction,
    float3 light_illuminance,
    out float3 throughput)
{
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[pass.multi_scattering_lut_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const VortexSingleScatteringResult scattering = VortexIntegrateSingleScatteredLuminance(
        ray_origin,
        ray_direction,
        false,
        max(1.0f, (float)pass.depth_resolution * 0.5f),
        1.0f,
        1.0f,
        0.0f,
        light_direction,
        float3(0.0f, 0.0f, 1.0f),
        light_illuminance,
        0.0f.xxx,
        pass.aerial_perspective_distance_scale,
        atmosphere,
        pass.transmittance_lut_srv,
        (float)pass.transmittance_width,
        (float)pass.transmittance_height,
        multi_scat_lut,
        linear_sampler,
        ray_length);
    throughput = scattering.Transmittance;
    return scattering.L;
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
        || pass.transmittance_lut_srv == K_INVALID_BINDLESS_INDEX
        || pass.multi_scattering_lut_srv == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_width
        || dispatch_id.y >= pass.output_height
        || dispatch_id.z >= pass.output_depth)
    {
        return;
    }

    RWTexture3D<float4> output_texture = ResourceDescriptorHeap[pass.output_texture_uav];
    const GpuSkyAtmosphereParams atmosphere = BuildAtmosphereParams(pass);
    const float2 uv = (float2(dispatch_id.xy) + 0.5f)
        / float2(pass.output_width, pass.output_height);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 clip_pos = float4(ndc, 1.0f, 1.0f);
    const float4 view_pos = mul(projection_matrix, clip_pos);
    (void)view_pos;

    const float far_depth = ResolveFarDepthReference();
    const float3 far_world_position = ReconstructWorldPosition(
        uv,
        far_depth,
        inverse_view_projection_matrix);
    float3 world_direction = VortexSafeNormalize(far_world_position - camera_position);

    const float slice = ((float(dispatch_id.z) + 0.5f) * pass.depth_resolution_inv);
    float non_linear_slice = slice;
    non_linear_slice *= non_linear_slice;
    non_linear_slice *= pass.depth_resolution;

    float ray_length_m = non_linear_slice * pass.depth_slice_length_km * 1000.0f;
    ray_length_m *= max(pass.aerial_perspective_distance_scale, 0.0f);
    if (ray_length_m <= 1.0e-4f)
    {
        output_texture[dispatch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const float3 camera_planet_position = pass.camera_planet_position.xyz;
    float3 ray_origin = camera_planet_position
        + world_direction * (pass.start_depth_km * 1000.0f);
    const float atmosphere_radius = atmosphere.planet_radius_m + atmosphere.atmosphere_height_m;
    if (!MoveToTopAtmosphere(ray_origin, world_direction, atmosphere_radius))
    {
        output_texture[dispatch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const float3 light0_direction = VortexSafeNormalize(pass.light0_direction_enabled.xyz);
    const float3 light1_direction = VortexSafeNormalize(pass.light1_direction_enabled.xyz);

    float3 throughput = 1.0f.xxx;
    float3 luminance = 0.0f.xxx;

    if (pass.light0_direction_enabled.w > 0.5f)
    {
        float3 local_throughput = 1.0f.xxx;
        luminance += IntegrateCameraAerialLight(
            atmosphere,
            pass,
            ray_origin,
            world_direction,
            ray_length_m,
            light0_direction,
            pass.light0_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz,
            local_throughput);
        throughput = local_throughput;
    }

    if (pass.light1_direction_enabled.w > 0.5f)
    {
        float3 local_throughput = 1.0f.xxx;
        luminance += IntegrateCameraAerialLight(
            atmosphere,
            pass,
            ray_origin,
            world_direction,
            ray_length_m,
            light1_direction,
            pass.light1_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz,
            local_throughput);
        throughput = local_throughput;
    }

    const float transmittance = dot(throughput, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));
    output_texture[dispatch_id] = float4(
        max(luminance * pass.aerial_scattering_strength, 0.0f.xxx),
        saturate(transmittance));
}

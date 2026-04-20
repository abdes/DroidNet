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
    float sample_count_per_slice;
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
        pass.aerial_perspective_distance_scale,
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

static float ResolveFarDepthReference()
{
    return reverse_z != 0u ? 0.0f : 1.0f;
}

static float ResolveNearDepthReference()
{
    return reverse_z != 0u ? 1.0f : 0.0f;
}

static bool IsOrthoProjection()
{
    return is_orthographic != 0u;
}

static VortexSingleScatteringResult IntegrateCameraAerialLight(
    GpuSkyAtmosphereParams atmosphere,
    AtmosphereCameraAerialPerspectivePassConstants pass,
    float3 ray_origin,
    float3 ray_direction,
    float ray_length,
    float sample_count)
{
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[pass.multi_scattering_lut_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    return VortexIntegrateSingleScatteredLuminance(
        ray_origin,
        ray_direction,
        false,
        true,
        true,
        false,
        max(1.0f, sample_count),
        1.0f,
        1.0f,
        0.0f,
        pass.light0_direction_enabled.w > 0.5f ? VortexSafeNormalize(pass.light0_direction_enabled.xyz) : float3(0.0f, 0.0f, 1.0f),
        pass.light1_direction_enabled.w > 0.5f ? VortexSafeNormalize(pass.light1_direction_enabled.xyz) : float3(0.0f, 0.0f, 1.0f),
        pass.light0_direction_enabled.w > 0.5f ? pass.light0_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz : 0.0f.xxx,
        pass.light1_direction_enabled.w > 0.5f ? pass.light1_illuminance_rgb.xyz * pass.sky_and_aerial_luminance_factor_rgb.xyz : 0.0f.xxx,
        pass.aerial_perspective_distance_scale,
        atmosphere,
        pass.transmittance_lut_srv,
        (float)pass.transmittance_width,
        (float)pass.transmittance_height,
        multi_scat_lut,
        linear_sampler,
        ray_length);
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
    const float far_depth = ResolveFarDepthReference();
    float3 far_world_position = ReconstructWorldPosition(
        uv,
        far_depth,
        inverse_view_projection_matrix);
    float3 world_direction = VortexSafeNormalize(far_world_position - camera_position);
    float3 camera_planet_position = pass.camera_planet_position.xyz;
    if (IsOrthoProjection())
    {
        const float3 near_world_position = ReconstructWorldPosition(
            uv,
            ResolveNearDepthReference(),
            inverse_view_projection_matrix);
        world_direction = VortexSafeNormalize(far_world_position - near_world_position);
        camera_planet_position += near_world_position - camera_position;
    }

    const float slice = ((float(dispatch_id.z) + 0.5f) * pass.depth_resolution_inv);
    float non_linear_slice = slice;
    non_linear_slice *= non_linear_slice;
    non_linear_slice *= pass.depth_resolution;

    const float start_depth_m = pass.start_depth_km * 1000.0f;
    float3 ray_origin = camera_planet_position + world_direction * start_depth_m;
    float t_max_max = non_linear_slice * pass.depth_slice_length_km * 1000.0f;
    if (t_max_max <= 1.0e-4f)
    {
        output_texture[dispatch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float3 voxel_world_pos = ray_origin + t_max_max * world_direction;
    const float voxel_height = length(voxel_world_pos);
    const bool underground = voxel_height < atmosphere.planet_radius_m;

    const float3 camera_to_voxel = voxel_world_pos - camera_planet_position;
    const float camera_to_voxel_len = length(camera_to_voxel);
    const float3 camera_to_voxel_dir = camera_to_voxel_len > 1.0e-6f
        ? camera_to_voxel / camera_to_voxel_len
        : world_direction;
    const float planet_near_t = RaySphereIntersectNearest(
        camera_planet_position,
        camera_to_voxel_dir,
        atmosphere.planet_radius_m);
    const bool below_horizon = planet_near_t > 0.0f && camera_to_voxel_len > planet_near_t;

    if (below_horizon || underground)
    {
        camera_planet_position += normalize(camera_planet_position) * 20.0f;

        const float3 voxel_world_pos_norm = normalize(voxel_world_pos);
        const float3 camera_proj_on_ground = normalize(camera_planet_position) * atmosphere.planet_radius_m;
        const float3 voxel_proj_on_ground = voxel_world_pos_norm * atmosphere.planet_radius_m;
        const float3 voxel_ground_to_ray_start = camera_planet_position - voxel_proj_on_ground;
        if (below_horizon && dot(normalize(voxel_ground_to_ray_start), voxel_world_pos_norm) < 0.0001f)
        {
            const float3 middle_point = 0.5f * (camera_proj_on_ground + voxel_proj_on_ground);
            const float3 middle_point_on_ground = normalize(middle_point) * atmosphere.planet_radius_m;
            voxel_world_pos = camera_planet_position + 2.0f * (middle_point_on_ground - camera_planet_position);
        }
        else if (underground)
        {
            voxel_world_pos = normalize(voxel_world_pos) * atmosphere.planet_radius_m;
        }

        world_direction = VortexSafeNormalize(voxel_world_pos - camera_planet_position);
        ray_origin = camera_planet_position + start_depth_m * world_direction;
        t_max_max = length(voxel_world_pos - ray_origin);
    }

    const float atmosphere_radius = atmosphere.planet_radius_m + atmosphere.atmosphere_height_m;
    const float view_height = length(ray_origin);
    if (view_height >= atmosphere_radius)
    {
        const float3 previous_ray_origin = ray_origin;
        if (!MoveToTopAtmosphere(ray_origin, world_direction, atmosphere_radius))
        {
            output_texture[dispatch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }

        const float length_to_atmosphere = length(previous_ray_origin - ray_origin);
        if (t_max_max < length_to_atmosphere)
        {
            output_texture[dispatch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }
        t_max_max = max(0.0f, t_max_max - length_to_atmosphere);
    }

    if (t_max_max <= 1.0e-4f)
    {
        output_texture[dispatch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const float sample_count = max(
        1.0f,
        (float(dispatch_id.z) + 1.0f) * max(pass.sample_count_per_slice, 1.0f));

    const VortexSingleScatteringResult scattering = IntegrateCameraAerialLight(
        atmosphere,
        pass,
        ray_origin,
        world_direction,
        t_max_max,
        sample_count);
    const float3 luminance = scattering.L;
    const float3 throughput = scattering.Transmittance;
    const float transmittance = dot(throughput, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));
    output_texture[dispatch_id] = float4(
        max(luminance * pass.aerial_scattering_strength, 0.0f.xxx),
        saturate(transmittance));
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/EnvironmentViewHelpers.hlsli"
#include "Renderer/ViewColorHelpers.hlsli"
#include "Renderer/ViewConstants.hlsli"

#include "Vortex/Contracts/SceneTextures.hlsli"
#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static inline bool IsReverseZProjection()
{
    return reverse_z != 0u;
}

static inline float ResolveFarDepthReference()
{
    return IsReverseZProjection() ? 0.0f : 1.0f;
}

static inline float EvaluateFarBackgroundMask(float scene_depth)
{
    const float far_depth = ResolveFarDepthReference();
    const float epsilon = 1.0e-3f;
    return saturate(1.0f - abs(scene_depth - far_depth) / epsilon);
}

static inline bool IsFarBackgroundPixel(float scene_depth)
{
    return scene_depth == ResolveFarDepthReference();
}

static inline float3 ReconstructViewDirection(float2 uv)
{
    const float3 far_world_position = ReconstructWorldPosition(
        uv, ResolveFarDepthReference(), inverse_view_projection_matrix);
    const float3 view_vector = far_world_position - camera_position;
    const float distance_to_sample = length(view_vector);
    return distance_to_sample > 1.0e-4f
        ? view_vector / distance_to_sample
        : normalize(float3(uv - 0.5f, 1.0f));
}

static inline bool IsAtmosphereRenderedInMain(EnvironmentViewData environment_view)
{
    return environment_view
        .trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass.w > 0.5f;
}

static const uint kEnvironmentViewFlagReflectionCapture = 1u << 1u;

static inline bool IsReflectionCaptureView(EnvironmentViewData environment_view)
{
    return (environment_view.flags & kEnvironmentViewFlagReflectionCapture) != 0u;
}

static float3 ClampPreExposedDiskLuminancePreserveChromaticity(float3 pre_exposed_luminance)
{
    // Oxygen keeps the sun in physical lux. After converting illuminance to
    // disk luminance via solid angle, the visible disk can reach values far
    // above the FP16 scene-color range. A per-channel clamp destroys the RGB
    // ratios and makes the disk turn white or behave erratically when multiple
    // channels are driven. Clamp uniformly instead so chromaticity is preserved.
    const float max_pre_exposed_component = max(
        max(pre_exposed_luminance.x, pre_exposed_luminance.y),
        pre_exposed_luminance.z);
    const float safe_max_pre_exposed_luminance = 64000.0f;
    if (max_pre_exposed_component <= safe_max_pre_exposed_luminance)
    {
        return pre_exposed_luminance;
    }

    const float scale = safe_max_pre_exposed_luminance
        / max(max_pre_exposed_component, 1.0e-6f);
    return pre_exposed_luminance * scale;
}

static inline float2 ResolveSkyViewUvFromLocalDirection(
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 view_direction_local,
    out float view_height,
    out float bottom_radius,
    out float top_radius)
{
    view_height = environment_view.sky_planet_translated_world_center_and_view_height.w;
    bottom_radius = env_data.atmosphere.planet_radius_m;
    top_radius = env_data.atmosphere.planet_radius_m + env_data.atmosphere.atmosphere_height_m;

    const float view_zenith_cos_angle = view_direction_local.z;
    const bool intersect_ground = RaySphereIntersectNearest(
        float3(0.0f, 0.0f, view_height),
        view_direction_local,
        bottom_radius) >= 0.0f;
    const float2 sky_view_lut_inv_size = float2(
        env_data.atmosphere.sky_view_lut_width > 0.0f
            ? rcp(env_data.atmosphere.sky_view_lut_width)
            : 0.0f,
        env_data.atmosphere.sky_view_lut_height > 0.0f
            ? rcp(env_data.atmosphere.sky_view_lut_height)
            : 0.0f);
    const float2 sky_view_lut_size = float2(
        env_data.atmosphere.sky_view_lut_width,
        env_data.atmosphere.sky_view_lut_height);
    return SkyViewLutParamsToUv(
        intersect_ground,
        view_zenith_cos_angle,
        view_direction_local,
        view_height,
        bottom_radius,
        sky_view_lut_size,
        sky_view_lut_inv_size);
}

static inline float2 ResolveSkyViewUv(
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 view_direction,
    out float3 view_direction_local,
    out float view_height,
    out float bottom_radius,
    out float top_radius)
{
    view_direction_local = ApplySkyViewLutReferential(environment_view, view_direction);
    return ResolveSkyViewUvFromLocalDirection(
        env_data,
        environment_view,
        view_direction_local,
        view_height,
        bottom_radius,
        top_radius);
}

static float3 GetAtmosphereTransmittance(
    float3 planet_center_to_world_pos,
    float3 world_dir,
    GpuSkyAtmosphereParams atmo,
    uint transmittance_lut_srv)
{
    const float ground_hit = RaySphereIntersectNearest(
        planet_center_to_world_pos,
        world_dir,
        atmo.planet_radius_m);
    if (ground_hit > 0.0f)
    {
        return 0.0f.xxx;
    }

    const float p_height = length(planet_center_to_world_pos);
    const float3 up_vector = planet_center_to_world_pos / max(p_height, 1.0e-4f);
    const float light_zenith_cos_angle = dot(world_dir, up_vector);
    const float altitude_m = max(p_height - atmo.planet_radius_m, 0.0f);

    const float3 transmittance_to_light = VortexSampleTransmittanceLut(
        transmittance_lut_srv,
        atmo.transmittance_lut_width,
        atmo.transmittance_lut_height,
        light_zenith_cos_angle,
        altitude_m,
        atmo.planet_radius_m,
        atmo.atmosphere_height_m);
    return transmittance_to_light;
}

static float3 GetLightDiskLuminance(
    float3 planet_center_to_camera,
    float3 world_dir,
    GpuSkyAtmosphereParams atmo,
    uint transmittance_lut_srv,
    float3 atmosphere_light_direction,
    float atmosphere_light_disc_cos_half_apex_angle,
    float3 atmosphere_light_disc_luminance)
{
    const float view_dot_light = dot(world_dir, atmosphere_light_direction);
    const float cos_half_apex = atmosphere_light_disc_cos_half_apex_angle;
    if (view_dot_light > cos_half_apex)
    {
        const float3 transmittance_to_light = GetAtmosphereTransmittance(
            planet_center_to_camera,
            world_dir,
            atmo,
            transmittance_lut_srv);
        const float soft_edge = saturate(
            2.0f * (view_dot_light - cos_half_apex) / max(1.0f - cos_half_apex, 1.0e-4f));
        return transmittance_to_light * atmosphere_light_disc_luminance * soft_edge;
    }
    return 0.0f.xxx;
}

[shader("vertex")]
VortexFullscreenTriangleOutput VortexSkyPassVS(uint vertex_id : SV_VertexID)
{
    VortexFullscreenTriangleOutput output = GenerateVortexFullscreenTriangle(vertex_id);
    output.position.z = ResolveFarDepthReference();
    return output;
}

[shader("pixel")]
float4 VortexSkyPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data))
    {
        discard;
    }
    if (env_data.atmosphere.sky_view_lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        discard;
    }

    const EnvironmentViewData environment_view = LoadResolvedEnvironmentViewData();
    if (!IsAtmosphereRenderedInMain(environment_view))
    {
        discard;
    }
    const float3 view_direction = ReconstructViewDirection(input.uv);

    float3 view_direction_local = 0.0f.xxx;
    float view_height = 0.0f;
    float bottom_radius = 0.0f;
    float top_radius = 0.0f;
    const float2 uv = ResolveSkyViewUv(
        env_data,
        environment_view,
        view_direction,
        view_direction_local,
        view_height,
        bottom_radius,
        top_radius);

    Texture2D<float4> sky_view_lut = ResourceDescriptorHeap[env_data.atmosphere.sky_view_lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const float view_pre_exposure = max(GetExposure(), 1.0e-6f);
    const float view_one_over_pre_exposure = rcp(view_pre_exposure);
    const float4 sky_sample = sky_view_lut.SampleLevel(linear_sampler, uv, 0.0f);
    float3 sky_color = max(
        sky_sample.rgb * environment_view.sky_luminance_factor_height_fog_contribution.xyz,
        0.0f.xxx);

    const float3 planet_center_to_camera = float3(0.0f, 0.0f, view_height);
    const bool reflection_capture_view = IsReflectionCaptureView(environment_view);
    const bool light0_disk_enabled = !reflection_capture_view
        && env_data.atmosphere.sun_disk_enabled != 0u
        && env_data.atmosphere.transmittance_lut_slot != K_INVALID_BINDLESS_INDEX
        && environment_view.atmosphere_light0_disk_luminance_rgb.w > 0.5f;
    const bool light1_disk_enabled = !reflection_capture_view
        && env_data.atmosphere.sun_disk_enabled != 0u
        && env_data.atmosphere.transmittance_lut_slot != K_INVALID_BINDLESS_INDEX
        && environment_view.atmosphere_light1_disk_luminance_rgb.w > 0.5f;

    if (light0_disk_enabled)
    {
        const float cos_half_apex = cos(environment_view.atmosphere_light0_direction_angular_size.w);
        const float3 light_direction_local = ApplySkyViewLutReferential(
            environment_view,
            VortexSafeNormalize(environment_view.atmosphere_light0_direction_angular_size.xyz));
        const float3 disk_luminance_pre_exposed = GetLightDiskLuminance(
            planet_center_to_camera,
            view_direction_local,
            env_data.atmosphere,
            env_data.atmosphere.transmittance_lut_slot,
            light_direction_local,
            cos_half_apex,
            environment_view.atmosphere_light0_disk_luminance_rgb.xyz) * view_pre_exposure;
        sky_color += ClampPreExposedDiskLuminancePreserveChromaticity(
            disk_luminance_pre_exposed);
    }
    if (light1_disk_enabled)
    {
        const float cos_half_apex = cos(environment_view.atmosphere_light1_direction_angular_size.w);
        const float3 light_direction_local = ApplySkyViewLutReferential(
            environment_view,
            VortexSafeNormalize(environment_view.atmosphere_light1_direction_angular_size.xyz));
        const float3 disk_luminance_pre_exposed = GetLightDiskLuminance(
            planet_center_to_camera,
            view_direction_local,
            env_data.atmosphere,
            env_data.atmosphere.transmittance_lut_slot,
            light_direction_local,
            cos_half_apex,
            environment_view.atmosphere_light1_disk_luminance_rgb.xyz) * view_pre_exposure;
        sky_color += ClampPreExposedDiskLuminancePreserveChromaticity(
            disk_luminance_pre_exposed);
    }

    return float4(sky_color * view_one_over_pre_exposure, sky_sample.a);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexIblIrradianceCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    (void)dispatch_id;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexIblPrefilterCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    (void)dispatch_id;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Contracts/View/ViewColorHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Services/Environment/ParityTransmittance.hlsli"
#include "Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static const uint kSkySphereSourceCubemap = 0u;
static const uint kSkySphereSourceSolidColor = 1u;

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

static inline float3 RotateDirectionAroundOxygenUp(float3 direction, float radians)
{
    const float c = cos(radians);
    const float s = sin(radians);
    return float3(
        c * direction.x - s * direction.y,
        s * direction.x + c * direction.y,
        direction.z);
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

static float3 ClampPreExposedDiskLuminance(float3 pre_exposed_luminance)
{
    // Mirrors UE5.7 SkyAtmosphere.usf::GetLightDiskLuminance: clamp the
    // pre-exposed disk luminance per channel to 64000 to avoid overflow and
    // pathological TAA input from the solar disk.
    return min(pre_exposed_luminance, 64000.0f.xxx);
}

static inline float2 ResolveSkyViewUvFromLocalDirection(
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 view_direction_local,
    out float view_height,
    out float bottom_radius,
    out float top_radius)
{
    view_height = environment_view.sky_planet_translated_world_center_km_and_view_height_km.w;
    bottom_radius = env_data.atmosphere.planet_radius_km;
    top_radius = env_data.atmosphere.planet_radius_km + env_data.atmosphere.atmosphere_height_km;

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
    return AnalyticalPlanetOccludedTransmittance(
        planet_center_to_world_pos,
        world_dir,
        transmittance_lut_srv,
        atmo.transmittance_lut_width,
        atmo.transmittance_lut_height,
        atmo.planet_radius_km,
        atmo.atmosphere_height_km);
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

    const float3 view_direction = ReconstructViewDirection(input.uv);
    if (env_data.sky_sphere.enabled != 0u)
    {
        float3 sky_color = 0.0f.xxx;
        if (env_data.sky_sphere.source == kSkySphereSourceSolidColor)
        {
            sky_color = env_data.sky_sphere.solid_color_rgb;
        }
        else if (env_data.sky_sphere.source == kSkySphereSourceCubemap
            && env_data.sky_sphere.cubemap_slot != K_INVALID_BINDLESS_INDEX
            && BX_IN_GLOBAL_SRV(env_data.sky_sphere.cubemap_slot))
        {
            TextureCube<float4> sky_cube =
                ResourceDescriptorHeap[env_data.sky_sphere.cubemap_slot];
            const float3 rotated_direction = RotateDirectionAroundOxygenUp(
                view_direction, env_data.sky_sphere.rotation_radians);
            SamplerState linear_sampler =
                SamplerDescriptorHeap[kAtmosphereLinearClampSampler];
            sky_color = sky_cube.SampleLevel(
                linear_sampler,
                CubemapSamplingDirFromOxygenWS(rotated_direction),
                0.0f).rgb;
        }
        else
        {
            discard;
        }

        sky_color *= env_data.sky_sphere.tint_rgb
            * max(env_data.sky_sphere.intensity, 0.0f);
        return float4(max(sky_color, 0.0f.xxx), 1.0f);
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
    // UE5.7 samples the sky-view LUT with bilinear clamp. Even though sky-view
    // azimuth is conceptually circular, keeping the shared clamp sampler here
    // avoids drifting away from the LUT contract used by the rest of the
    // atmosphere pipeline.
    SamplerState linear_sampler
        = SamplerDescriptorHeap[kAtmosphereLinearClampSampler];
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
        sky_color += ClampPreExposedDiskLuminance(
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
        sky_color += ClampPreExposedDiskLuminance(
            disk_luminance_pre_exposed);
    }

    // Mirrors UE5.7 SkyAtmosphere.usf::PrepareOutput (line 869): clamp the
    // pre-exposed luminance per channel to Max10BitsFloat*0.5 (32256) so the
    // sky stays within fp10 range and leaves headroom for bloom/clouds/etc.
    // Max10BitsFloat is defined in UE Common.ush:144 as 64512.0f.
    sky_color = min(sky_color, 32256.0f.xxx);
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

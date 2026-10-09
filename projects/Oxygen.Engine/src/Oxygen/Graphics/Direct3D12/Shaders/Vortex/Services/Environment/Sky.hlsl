//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Shared/CubemapGeometry.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/View/HdrConsumerInputs.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Services/Environment/SkyRadiance.hlsli"
#include "Vortex/Services/Environment/HeightFog.hlsli"
#include "Vortex/Services/Environment/ParityTransmittance.hlsli"
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

// TODO(post-v0.1, EV01-CELESTIAL-SURFACE): Add celestial surface/phase shading.
// This helper renders an analytic disk; Secondary does not imply a phased Moon.
// Scope: design/vortex/milestones/ED-M08/deferred-capabilities.md#ev01-celestial-surface
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
    const float edge_width = max(fwidth(view_dot_light), 1.17549435e-38f);
    if (view_dot_light > cos_half_apex)
    {
        const float3 transmittance_to_light = GetAtmosphereTransmittance(
            planet_center_to_camera,
            world_dir,
            atmo,
            transmittance_lut_srv);
        const float soft_edge = saturate(
            (view_dot_light - cos_half_apex) / edge_width);
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
[earlydepthstencil]
float4 VortexSkyPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data))
    {
        discard;
    }

    const float3 view_direction = ReconstructViewDirection(input.uv);
    const EnvironmentViewData environment_view = LoadResolvedEnvironmentViewData();
    const float4 height_fog = EvaluateSkyHeightFog(env_data, environment_view,
        camera_position, view_direction, false);
    if (IsSkyHeightFogEnabled(env_data.fog, environment_view, false))
        RecordHdrConsumerInput(height_fog.rgb, HDR_INPUT_HEIGHT_FOG, HDR_CONSUMER_SKY_HEIGHT_FOG);
    const float view_pre_exposure = GetPreExposure();
    if (env_data.sky_sphere.enabled != 0u)
    {
        float3 sky_color = 0.0f.xxx;
        if (env_data.sky_sphere.source == kSkySphereSourceSolidColor)
        {
            sky_color = env_data.sky_sphere.solid_color_rgb;
        }
        else if (env_data.sky_sphere.source == kSkySphereSourceCubemap
            && env_data.sky_sphere.cubemap_slot != K_INVALID_BINDLESS_INDEX
            && BX_IN_TEXTURES(env_data.sky_sphere.cubemap_slot))
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
        CheckHdrStoreRange(float4(sky_color, 1.0), 7u,
            LoadViewFrameBindings(bindless_view_frame_bindings_slot).exposure_status_uav, 0u, 1.0);
        sky_color = (max(sky_color, 0.0.xxx) * height_fog.a + height_fog.rgb) * view_pre_exposure;
        CheckHdrStoreRange(float4(sky_color, 1.0), 7u,
            LoadViewFrameBindings(bindless_view_frame_bindings_slot).exposure_status_uav, 0u, view_pre_exposure);
        return float4(sky_color, 1.0);
    }

    if (env_data.atmosphere.enabled == 0u
        || !BX_IN_TEXTURES(env_data.atmosphere.sky_view_lut_slot)
        || !IsAtmosphereRenderedInMain(environment_view))
    {
        if (!IsSkyHeightFogEnabled(env_data.fog, environment_view, false)) discard;
        // Existing coverage composition places the display-only background
        // behind this fog without admitting that background to sky lighting.
        const float3 fog_color = height_fog.rgb * view_pre_exposure;
        CheckHdrStoreRange(float4(fog_color, 1.0), 7u,
            LoadViewFrameBindings(bindless_view_frame_bindings_slot).exposure_status_uav, 0u, view_pre_exposure);
        return float4(fog_color, 1.0 - height_fog.a);
    }

    const float view_height = environment_view.sky_planet_translated_world_center_km_and_view_height_km.w;
    const float3 view_direction_local = ApplySkyViewLutReferential(environment_view, view_direction);
    RecordHdrConsumerUsage(HDR_CONSUMER_SKY,
        environment_view.sky_luminance_factor_height_fog_contribution.xyz);
    const float4 sky_sample = SampleSkyViewRadiance(env_data, environment_view, view_direction);
    const bool reflection_capture_view = IsReflectionCaptureView(environment_view);
    const bool atmosphere_holdout = !reflection_capture_view
        && environment_view.trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass.z > 0.5f;
    float3 sky_color = atmosphere_holdout ? 0.0f.xxx : max(sky_sample.rgb, 0.0.xxx);

    const float3 planet_center_to_camera = float3(0.0f, 0.0f, view_height);
    const bool light0_disk_enabled = !reflection_capture_view && !atmosphere_holdout
        && env_data.atmosphere.sun_disk_enabled != 0u
        && env_data.atmosphere.transmittance_lut_slot != K_INVALID_BINDLESS_INDEX
        && environment_view.atmosphere_light0_disk_luminance_rgb.w > 0.5f;
    const bool light1_disk_enabled = !reflection_capture_view && !atmosphere_holdout
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
        sky_color += disk_luminance_pre_exposed;
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
        sky_color += disk_luminance_pre_exposed;
    }

    sky_color = sky_color * height_fog.a + height_fog.rgb * view_pre_exposure;
    const float coverage = atmosphere_holdout
        ? saturate(1.0f - saturate(sky_sample.a) * height_fog.a) : 1.0f;
    CheckHdrStoreRange(float4(sky_color, coverage), 7u,
        LoadViewFrameBindings(bindless_view_frame_bindings_slot).exposure_status_uav,
        0u, view_pre_exposure);
    return float4(sky_color, coverage);
}

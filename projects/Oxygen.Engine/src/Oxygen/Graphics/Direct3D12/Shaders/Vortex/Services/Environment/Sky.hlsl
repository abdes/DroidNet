//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/EnvironmentFrameBindings.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/EnvironmentViewHelpers.hlsli"
#include "Renderer/ViewConstants.hlsli"

#include "Vortex/Contracts/SceneTextures.hlsli"
#include "Vortex/Contracts/ViewFrameBindings.hlsli"
#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static inline bool IsReverseZProjection()
{
    return projection_matrix._33 > 0.0f;
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

static inline EnvironmentFrameBindings LoadResolvedEnvironmentBindings()
{
    const ViewFrameBindingsData view_bindings = LoadVortexViewFrameBindings(
        bindless_view_frame_bindings_slot);
    return LoadEnvironmentFrameBindings(view_bindings.environment_frame_slot);
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

static inline float2 ResolveSkyViewUv(
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 view_direction,
    out float3 view_direction_local,
    out float view_height,
    out float bottom_radius,
    out float top_radius)
{
    view_height = environment_view.sky_planet_translated_world_center_and_view_height.w;
    bottom_radius = env_data.atmosphere.planet_radius_m;
    top_radius = env_data.atmosphere.planet_radius_m + env_data.atmosphere.atmosphere_height_m;

    view_direction_local = ApplySkyViewLutReferential(environment_view, view_direction);
    const float view_zenith_cos_angle = view_direction_local.z;
    const bool intersect_ground = RaySphereIntersectNearest(
        float3(0.0f, 0.0f, view_height),
        view_direction_local,
        bottom_radius) >= 0.0f;
    return SkyViewLutParamsToUv(
        intersect_ground,
        view_zenith_cos_angle,
        view_direction_local,
        view_height,
        bottom_radius,
        float2(1.0f / 192.0f, 1.0f / 104.0f));
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

    const float3 optical_depth = VortexSampleTransmittanceOpticalDepthLut(
        transmittance_lut_srv,
        atmo.transmittance_lut_width,
        atmo.transmittance_lut_height,
        light_zenith_cos_angle,
        altitude_m,
        atmo.planet_radius_m,
        atmo.atmosphere_height_m);
    return VortexTransmittanceFromOpticalDepth(optical_depth, atmo);
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
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexSkyPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const SceneTextureBindingData bindings
        = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    const float scene_depth = SampleSceneDepth(input.uv, bindings);
    if (EvaluateFarBackgroundMask(scene_depth) <= 0.0f) {
        discard;
    }

    const EnvironmentFrameBindings environment_bindings = LoadResolvedEnvironmentBindings();
    if (environment_bindings.sky_view_lut_srv == K_INVALID_BINDLESS_INDEX)
    {
        discard;
    }

    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data))
    {
        discard;
    }

    const EnvironmentViewData environment_view = LoadResolvedEnvironmentViewData();
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

    Texture2D<float4> sky_view_lut = ResourceDescriptorHeap[environment_bindings.sky_view_lut_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    float4 sky_sample = sky_view_lut.SampleLevel(linear_sampler, uv, 0.0f);
    float3 sky_color = max(
        sky_sample.rgb * environment_view.sky_luminance_factor_height_fog_contribution.xyz,
        0.0f.xxx);

    const float3 planet_center_to_camera = float3(0.0f, 0.0f, view_height);
    if (environment_bindings.atmosphere_light0_disk_luminance_rgb.w > 0.5f)
    {
        const float cos_half_apex = cos(0.5f * environment_bindings.atmosphere_light0_direction_angular_size.w);
        sky_color += GetLightDiskLuminance(
            planet_center_to_camera,
            view_direction_local,
            env_data.atmosphere,
            environment_bindings.transmittance_lut_srv,
            VortexSafeNormalize(environment_bindings.atmosphere_light0_direction_angular_size.xyz),
            cos_half_apex,
            environment_bindings.atmosphere_light0_disk_luminance_rgb.xyz);
    }
    if (environment_bindings.atmosphere_light1_disk_luminance_rgb.w > 0.5f)
    {
        const float cos_half_apex = cos(0.5f * environment_bindings.atmosphere_light1_direction_angular_size.w);
        sky_color += GetLightDiskLuminance(
            planet_center_to_camera,
            view_direction_local,
            env_data.atmosphere,
            environment_bindings.transmittance_lut_srv,
            VortexSafeNormalize(environment_bindings.atmosphere_light1_direction_angular_size.xyz),
            cos_half_apex,
            environment_bindings.atmosphere_light1_disk_luminance_rgb.xyz);
    }

    return float4(sky_color, 1.0f);
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

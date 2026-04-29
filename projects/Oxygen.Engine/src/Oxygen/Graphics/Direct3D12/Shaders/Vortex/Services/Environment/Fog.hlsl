//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static const float kFogEpsilon = 0.001f;
static const float kFogEpsilon2 = 0.01f;
static const float kPi = 3.14159265358979323846f;
static const float kUniformPhaseFunction = 0.07957747154594767f;

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

static inline bool FogFlagEnabled(uint flags, uint bit)
{
    return (flags & bit) != 0u;
}

static inline bool VolumetricFogFlagEnabled(uint flags, uint bit)
{
    return (flags & bit) != 0u;
}

static float CalculateLineIntegralShared(
    float fog_height_falloff,
    float ray_direction_z,
    float ray_origin_terms)
{
    const float falloff = max(-127.0f, fog_height_falloff * ray_direction_z);
    const float line_integral = (1.0f - exp2(-falloff)) / falloff;
    const float line_integral_taylor =
        log(2.0f) - (0.5f * log(2.0f) * log(2.0f)) * falloff;
    return ray_origin_terms
        * (abs(falloff) > kFogEpsilon2 ? line_integral : line_integral_taylor);
}

static float PrecomputeFogOriginFactor(
    float origin_height,
    float fog_height,
    float fog_falloff,
    float fog_density)
{
    const float collapsed_power = clamp(
        -fog_falloff * (origin_height - fog_height),
        -125.0f,
        126.0f);
    return fog_density * exp2(collapsed_power);
}

static float ComputeHeightFogLineIntegral(
    GpuFogParams fog,
    float3 world_camera_origin,
    inout float3 camera_to_receiver,
    out float camera_to_receiver_length,
    out float3 camera_to_receiver_normalized)
{
    float max_observer_height = 3.402823466e+38f;
    if (fog.primary_density > 0.0f) {
        max_observer_height = min(max_observer_height, fog.primary_height_offset_m + 65536.0f);
    }
    if (fog.secondary_density > 0.0f) {
        max_observer_height = min(max_observer_height, fog.secondary_height_offset_m + 65536.0f);
    }
    const float3 world_observer_origin = float3(
        world_camera_origin.xy,
        min(world_camera_origin.z, max_observer_height));

    const float camera_to_receiver_len_xy_sqr =
        dot(camera_to_receiver.xy, camera_to_receiver.xy);
    if (fog.end_distance_m > 0.0f
        && camera_to_receiver_len_xy_sqr > fog.end_distance_m * fog.end_distance_m) {
        camera_to_receiver *= fog.end_distance_m / sqrt(max(1.0f, camera_to_receiver_len_xy_sqr));
    }

    camera_to_receiver.z += world_camera_origin.z - world_observer_origin.z;
    const float camera_to_receiver_length_sqr =
        dot(camera_to_receiver, camera_to_receiver);
    const float camera_to_receiver_length_inv =
        rsqrt(max(camera_to_receiver_length_sqr, 0.00000001f));
    camera_to_receiver_length =
        camera_to_receiver_length_sqr * camera_to_receiver_length_inv;
    camera_to_receiver_normalized =
        camera_to_receiver * camera_to_receiver_length_inv;

    float ray_origin_terms = PrecomputeFogOriginFactor(
        world_observer_origin.z,
        fog.primary_height_offset_m,
        fog.primary_height_falloff,
        fog.primary_density);
    float ray_origin_terms_second = PrecomputeFogOriginFactor(
        world_observer_origin.z,
        fog.secondary_height_offset_m,
        fog.secondary_height_falloff,
        fog.secondary_density);
    float ray_length = camera_to_receiver_length;
    float ray_direction_z = camera_to_receiver.z;

    const float exclude_distance = max(fog.start_distance_m, 0.0f);
    if (exclude_distance > 0.0f && camera_to_receiver_length > kFogEpsilon) {
        const float exclude_intersection_time =
            saturate(exclude_distance * camera_to_receiver_length_inv);
        const float camera_to_exclusion_intersection_z =
            exclude_intersection_time * camera_to_receiver.z;
        const float exclusion_intersection_z =
            world_observer_origin.z + camera_to_exclusion_intersection_z;
        const float exclusion_intersection_to_receiver_z =
            camera_to_receiver.z - camera_to_exclusion_intersection_z;

        ray_length = (1.0f - exclude_intersection_time) * camera_to_receiver_length;
        ray_direction_z = exclusion_intersection_to_receiver_z;

        ray_origin_terms = PrecomputeFogOriginFactor(
            exclusion_intersection_z,
            fog.primary_height_offset_m,
            fog.primary_height_falloff,
            fog.primary_density);
        ray_origin_terms_second = PrecomputeFogOriginFactor(
            exclusion_intersection_z,
            fog.secondary_height_offset_m,
            fog.secondary_height_falloff,
            fog.secondary_density);
    }

    float line_integral_shared = CalculateLineIntegralShared(
        fog.primary_height_falloff,
        ray_direction_z,
        ray_origin_terms);
    line_integral_shared += CalculateLineIntegralShared(
        fog.secondary_height_falloff,
        ray_direction_z,
        ray_origin_terms_second);
    return line_integral_shared * ray_length;
}

static float3 GetViewDistanceSkyLightColor(EnvironmentStaticData env_data)
{
    if (env_data.atmosphere.distant_sky_light_lut_slot != K_INVALID_BINDLESS_INDEX
        && BX_IN_GLOBAL_SRV(env_data.atmosphere.distant_sky_light_lut_slot)) {
        StructuredBuffer<float4> distant_sky_light_lut =
            ResourceDescriptorHeap[env_data.atmosphere.distant_sky_light_lut_slot];
        return max(distant_sky_light_lut[0].rgb, 0.0f.xxx);
    }
    return 0.0f.xxx;
}

static float3 ComputeSkyAmbientContribution(
    GpuFogParams fog,
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view)
{
    const float height_fog_contribution =
        environment_view.sky_luminance_factor_height_fog_contribution.w;
    return fog.sky_atmosphere_ambient_contribution_color_scale_rgb
        * height_fog_contribution
        * GetViewDistanceSkyLightColor(env_data);
}

static float3 ComputeDirectionalInscatteringForLight(
    GpuFogParams fog,
    EnvironmentViewData environment_view,
    float3 camera_to_receiver_normalized,
    float ray_length,
    float line_integral,
    float4 light_direction_angular_size,
    float4 light_luminance_enabled)
{
    if (light_luminance_enabled.w <= 0.0f) {
        return 0.0f.xxx;
    }

    const float3 light_direction =
        normalize(light_direction_angular_size.xyz);
    const float directional_phase =
        pow(saturate(dot(camera_to_receiver_normalized, light_direction)),
            fog.directional_exponent) * kUniformPhaseFunction;
    const float dir_integral =
        line_integral * max(ray_length - fog.directional_start_distance_m, 0.0f)
        / max(ray_length, kFogEpsilon);
    const float directional_fog_factor = saturate(exp2(-dir_integral));
    const float angular_radius = max(light_direction_angular_size.w, 0.0f);
    const float solid_angle =
        2.0f * kPi * (1.0f - cos(angular_radius));
    const float3 light_illuminance_rgb =
        light_luminance_enabled.xyz * max(solid_angle, 1.0e-6f);
    const float height_fog_contribution =
        environment_view.sky_luminance_factor_height_fog_contribution.w;
    const float3 directional_color =
        fog.directional_inscattering_luminance_rgb
        + height_fog_contribution * light_illuminance_rgb;
    return directional_color * directional_phase * (1.0f - directional_fog_factor);
}

static float4 EvaluateExponentialHeightFog(
    GpuFogParams fog,
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 world_position)
{
    float3 camera_to_receiver = world_position - camera_position;
    float camera_to_receiver_length = 0.0f;
    float3 camera_to_receiver_normalized = 0.0f.xxx;
    const float line_integral = ComputeHeightFogLineIntegral(
        fog,
        camera_position,
        camera_to_receiver,
        camera_to_receiver_length,
        camera_to_receiver_normalized);

    float transmittance =
        max(saturate(exp2(-line_integral)), fog.min_transmittance);
    float3 directional_inscattering = 0.0f.xxx;

    if (fog.cutoff_distance_m > 0.0f
        && camera_to_receiver_length > fog.cutoff_distance_m) {
        transmittance = 1.0f;
    } else if (FogFlagEnabled(fog.flags, GPU_FOG_FLAG_DIRECTIONAL_INSCATTERING)) {
        directional_inscattering += ComputeDirectionalInscatteringForLight(
            fog,
            environment_view,
            camera_to_receiver_normalized,
            camera_to_receiver_length,
            line_integral,
            environment_view.atmosphere_light0_direction_angular_size,
            environment_view.atmosphere_light0_disk_luminance_rgb);
        directional_inscattering += ComputeDirectionalInscatteringForLight(
            fog,
            environment_view,
            camera_to_receiver_normalized,
            camera_to_receiver_length,
            line_integral,
            environment_view.atmosphere_light1_direction_angular_size,
            environment_view.atmosphere_light1_disk_luminance_rgb);
    }

    float3 inscattering_color =
        fog.fog_inscattering_luminance_rgb
        + ComputeSkyAmbientContribution(fog, env_data, environment_view);
    if (FogFlagEnabled(fog.flags, GPU_FOG_FLAG_CUBEMAP_USABLE)
        && fog.cubemap_srv != K_INVALID_BINDLESS_INDEX
        && BX_IN_GLOBAL_SRV(fog.cubemap_srv)) {
        const float fade_alpha =
            saturate(camera_to_receiver_length * fog.cubemap_fade_inv_range
                + fog.cubemap_fade_bias);
        inscattering_color *= lerp(
            fog.inscattering_texture_tint_rgb,
            1.0f.xxx,
            fade_alpha);
    }

    const float3 fog_color =
        inscattering_color * (1.0f - transmittance) + directional_inscattering;
    return float4(fog_color, transmittance);
}

static float4 SampleIntegratedVolumetricFog(
    GpuVolumetricFogParams volumetric_fog,
    float2 uv,
    float ray_length_m)
{
    if (!VolumetricFogFlagEnabled(volumetric_fog.flags, GPU_VOLUMETRIC_FOG_FLAG_ENABLED)
        || !VolumetricFogFlagEnabled(
            volumetric_fog.flags,
            GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID)
        || volumetric_fog.integrated_light_scattering_srv == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(volumetric_fog.integrated_light_scattering_srv)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float depth_span =
        max(volumetric_fog.distance_m - volumetric_fog.start_distance_m, 1.0f);
    const float linear_depth_fraction =
        saturate((ray_length_m - volumetric_fog.start_distance_m) / depth_span);
    float depth_fraction = sqrt(linear_depth_fraction);
    if (abs(volumetric_fog.grid_z_params.x) > 1.0e-8f
        && abs(volumetric_fog.grid_z_params.z) > 1.0e-4f
        && volumetric_fog.grid_depth > 0u) {
        const float z_argument = max(
            ray_length_m * volumetric_fog.grid_z_params.x
            + volumetric_fog.grid_z_params.y,
            1.0e-8f);
        const float z_slice =
            log2(z_argument) * volumetric_fog.grid_z_params.z;
        depth_fraction = saturate(
            z_slice / max(float(volumetric_fog.grid_depth), 1.0f));
    }
    Texture3D<float4> integrated_light_scattering =
        ResourceDescriptorHeap[volumetric_fog.integrated_light_scattering_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    return integrated_light_scattering.SampleLevel(
        linear_sampler,
        float3(uv, depth_fraction),
        0.0f);
}

static float4 ComposeFogResults(float4 height_fog, float4 volumetric_fog)
{
    return float4(
        volumetric_fog.rgb + height_fog.rgb * volumetric_fog.a,
        saturate(height_fog.a * volumetric_fog.a));
}

[shader("vertex")]
VortexFullscreenTriangleOutput VortexFogPassVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexFogPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const GpuFogParams fog = env_data.fog;
    if (!FogFlagEnabled(fog.flags, GPU_FOG_FLAG_ENABLED)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const EnvironmentViewData environment_view = LoadResolvedEnvironmentViewData();
    if (!FogFlagEnabled(fog.flags, GPU_FOG_FLAG_RENDER_IN_MAIN_PASS)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    const bool reflection_capture =
        (environment_view.flags & (1u << 1u)) != 0u;
    if (reflection_capture
        && !FogFlagEnabled(fog.flags, GPU_FOG_FLAG_VISIBLE_IN_REFLECTION_CAPTURES)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const SceneTextureBindingData bindings =
        LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    const float raw_depth = SampleSceneDepth(input.uv, bindings);
    if (EvaluateFarBackgroundMask(raw_depth) > 0.999f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float3 world_position = ReconstructWorldPosition(
        input.uv,
        raw_depth,
        inverse_view_projection_matrix);
    const float ray_length_m = length(world_position - camera_position);
    float4 height_fog = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (FogFlagEnabled(fog.flags, GPU_FOG_FLAG_HEIGHT_FOG_ENABLED)
        && (fog.primary_density > 0.0f || fog.secondary_density > 0.0f)) {
        height_fog = EvaluateExponentialHeightFog(
            fog,
            env_data,
            environment_view,
            world_position);
    }
    const float4 volumetric_fog = SampleIntegratedVolumetricFog(
        env_data.volumetric_fog,
        input.uv,
        ray_length_m);
    return ComposeFogResults(height_fog, volumetric_fog);
}

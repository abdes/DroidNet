//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_HEIGHT_FOG_HLSLI
#define OXYGEN_VORTEX_HEIGHT_FOG_HLSLI

#include "Vortex/Contracts/Environment/EnvironmentStaticData.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewData.hlsli"
#include "Vortex/Services/Environment/TransmittanceMath.hlsli"

static const float kFogEpsilon = 0.001f;
static const float kFogEpsilon2 = 0.01f;
static const float kPi = 3.14159265358979323846f;
static const float kUniformPhaseFunction = 0.07957747154594767f;

static inline bool FogFlagEnabled(uint flags, uint bit)
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
    if (env_data.atmosphere.enabled != 0u
        && env_data.atmosphere.distant_sky_light_lut_slot != K_INVALID_BINDLESS_INDEX
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
    float4 light_illuminance_enabled)
{
    if (light_illuminance_enabled.w <= 0.0f) {
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
    const float directional_opacity = saturate(OneMinusExpNegative(dir_integral * log(2.0f)));
    const float3 light_illuminance_rgb = light_illuminance_enabled.xyz;
    const float height_fog_contribution =
        environment_view.sky_luminance_factor_height_fog_contribution.w;
    const float3 directional_color =
        fog.directional_inscattering_luminance_rgb
        + height_fog_contribution * light_illuminance_rgb;
    return directional_color * directional_phase * directional_opacity;
}

static float4 EvaluateHeightFogSegment(
    GpuFogParams fog,
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 ray_origin,
    float3 camera_to_receiver)
{
    float camera_to_receiver_length = 0.0f;
    float3 camera_to_receiver_normalized = 0.0f.xxx;
    const float line_integral = ComputeHeightFogLineIntegral(
        fog,
        ray_origin,
        camera_to_receiver,
        camera_to_receiver_length,
        camera_to_receiver_normalized);

    float transmittance =
        max(saturate(exp2(-line_integral)), fog.min_transmittance);
    float opacity = min(saturate(OneMinusExpNegative(line_integral * log(2.0f))),
        1.0f - fog.min_transmittance);
    float3 directional_inscattering = 0.0f.xxx;

    if (fog.cutoff_distance_m > 0.0f
        && camera_to_receiver_length > fog.cutoff_distance_m) {
        transmittance = 1.0f;
        opacity = 0.0f;
    } else if (FogFlagEnabled(fog.flags, GPU_FOG_FLAG_DIRECTIONAL_INSCATTERING)) {
        directional_inscattering += ComputeDirectionalInscatteringForLight(
            fog,
            environment_view,
            camera_to_receiver_normalized,
            camera_to_receiver_length,
            line_integral,
            environment_view.atmosphere_light0_direction_angular_size,
            environment_view.height_fog_light0_illuminance_enabled);
        directional_inscattering += ComputeDirectionalInscatteringForLight(
            fog,
            environment_view,
            camera_to_receiver_normalized,
            camera_to_receiver_length,
            line_integral,
            environment_view.atmosphere_light1_direction_angular_size,
            environment_view.height_fog_light1_illuminance_enabled);
    }

    float3 inscattering_color =
        fog.fog_inscattering_luminance_rgb
        + ComputeSkyAmbientContribution(fog, env_data, environment_view);
    if (FogFlagEnabled(fog.flags, GPU_FOG_FLAG_CUBEMAP_USABLE)
        && fog.cubemap_srv != K_INVALID_BINDLESS_INDEX
        && BX_IN_TEXTURES(fog.cubemap_srv)) {
        const float fade_alpha =
            saturate(camera_to_receiver_length * fog.cubemap_fade_inv_range
                + fog.cubemap_fade_bias);
        inscattering_color *= lerp(
            fog.inscattering_texture_tint_rgb,
            1.0f.xxx,
            fade_alpha);
    }

    const float3 fog_color =
        inscattering_color * opacity + directional_inscattering;
    return float4(fog_color, transmittance);
}


static float4 EvaluateExponentialHeightFog(GpuFogParams fog,
    EnvironmentStaticData environment, EnvironmentViewData view,
    float3 origin, float3 direction, float distance)
{
    return EvaluateHeightFogSegment(fog, environment, view, origin, direction * distance);
}

// 90-degree virtual cube, 0.05 m near plane, infinite reversed-Z far ray.
// The maximum component is invariant under the native world/cube axis mapping.
static float HeightFogDistantRayDistance(float3 normalized_direction)
{
    float3 magnitude = abs(normalized_direction);
    return 0.05 / (1.0e-10 * max(magnitude.x, max(magnitude.y, magnitude.z)));
}

static bool IsSkyHeightFogEnabled(GpuFogParams fog, EnvironmentViewData view, bool capture)
{
    if (!FogFlagEnabled(fog.flags, GPU_FOG_FLAG_ENABLED)
        || !FogFlagEnabled(fog.flags, GPU_FOG_FLAG_HEIGHT_FOG_ENABLED)) return false;
    if (capture) return FogFlagEnabled(fog.flags, GPU_FOG_FLAG_VISIBLE_IN_REAL_TIME_SKY_CAPTURES);
    return (view.flags & ENVIRONMENT_VIEW_FLAG_HEIGHT_FOG) != 0u
        && FogFlagEnabled(fog.flags, GPU_FOG_FLAG_RENDER_IN_MAIN_PASS)
        && ((view.flags & (1u << 1u)) == 0u
            || FogFlagEnabled(fog.flags, GPU_FOG_FLAG_VISIBLE_IN_REFLECTION_CAPTURES));
}

static float4 EvaluateSkyHeightFog(EnvironmentStaticData environment,
    EnvironmentViewData view, float3 origin, float3 direction, bool capture)
{
    if (!IsSkyHeightFogEnabled(environment.fog, view, capture)) return float4(0, 0, 0, 1);
    return EvaluateExponentialHeightFog(environment.fog, environment, view,
        origin, direction, HeightFogDistantRayDistance(direction));
}

#endif

#ifndef OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI
#define OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI

#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Forward/ForwardPbr.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Common/Lighting.hlsli"
#include "Common/Geometry.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"

// Safety caps for fallback loops (pre-clustered culling).
#ifndef MAX_DIRECTIONAL_LIGHTS
#define MAX_DIRECTIONAL_LIGHTS 16
#endif

#ifndef MAX_POSITIONAL_LIGHTS
#define MAX_POSITIONAL_LIGHTS 1024
#endif

//! Computes sun transmittance for a surface point.
//!
//! Returns the atmospheric transmittance from the surface point toward the sun.
//! This accounts for both the sun's elevation and the surface altitude.
//! When atmosphere is disabled (atmo.enabled == 0), returns 1.0.
//!
//! In Oxygen's coordinate system (+Z up, -Y forward):
//! - World Z=0 is at the planet surface (ground level)
//! - Object altitude = world_pos.z (in the same units as planet_radius)
//! - Planet center is at (0, 0, -planet_radius) in world space
//!
//! @param world_pos World-space position of the surface.
//! @param atmo Atmosphere parameters from environment data.
//! @param sun_dir Direction toward the sun (normalized).
//! @return RGB transmittance (0 when sun below horizon, 1 when no atmosphere).
float3 ComputeSunTransmittance(
    float3 world_pos,
    GpuSkyAtmosphereParams atmo,
    float3 sun_dir)
{
    // No atmosphere = no attenuation.
    if (!atmo.enabled) {
        return float3(1.0, 1.0, 1.0);
    }

    // In Oxygen's convention:
    // - Ground is at Z=0, so altitude = world_pos.z
    // - Planet up is +Z
    // - The sun zenith angle is computed from the local vertical at the surface point
    //
    // For objects on/near the ground, the local "up" is approximately +Z.
    // For more precision, we could compute the actual radial direction from planet center,
    // but for typical camera altitudes (meters) vs planet radius (millions of meters),
    // treating up as +Z is accurate enough.

    float altitude = max(world_pos.z, 0.0);

    // Local up direction at this point on the planet surface.
    // For small altitudes compared to planet radius, this is effectively +Z.
    // For high altitudes or precision, use the radial direction from planet center.
    float3 planet_center = GetPlanetCenterWS();
    float3 to_surface = world_pos - planet_center;
    float height = length(to_surface);
    float3 local_up = to_surface / max(height, 1e-6);

    // Compute cosine of sun zenith from the local up direction
    float cos_sun_zenith = dot(local_up, sun_dir);

    // Hard geometric horizon guard: when sun is below the local horizon, direct
    // illumination must be zero, regardless of LUT readiness.
    const float cos_horizon = HorizonCosineFromAltitude(atmo.planet_radius_m, altitude);
    if (cos_sun_zenith < cos_horizon) {
        return float3(0.0, 0.0, 0.0);
    }

    // LUT missing/not ready: keep conservative passthrough only for above-horizon
    // sun to avoid blackouts during startup, while still honoring horizon occlusion.
    if (atmo.transmittance_lut_slot == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0, 1.0, 1.0);
    }

    // Sample transmittance LUT
    // The LUT returns high optical depth (zero transmittance) when sun is below horizon
    float3 optical_depth = SampleTransmittanceOpticalDepthLut(
        atmo.transmittance_lut_slot,
        atmo.transmittance_lut_width,
        atmo.transmittance_lut_height,
        cos_sun_zenith,
        altitude,
        atmo.planet_radius_m,
        atmo.atmosphere_height_m);

    // Convert optical depth to transmittance
    return TransmittanceFromOpticalDepth(optical_depth, atmo);
}

float3 AccumulateDirectionalLights(
    float3 world_pos,
    GpuSkyAtmosphereParams atmo,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    float3 direct = float3(0.0, 0.0, 0.0);

    // Render the Sun separately using the resolved EnvironmentDynamicData.sun
    bool sun_rendered = false;
    if (HasSunLight()) {
        const float3 sun_dir_ws = GetSunDirectionWS();
        const float3 sun_color = GetSunColorRGB();
        const float  sun_illuminance = GetSunIlluminance();

        const float3 L = SafeNormalize(sun_dir_ws);
        const float  NdotL = saturate(dot(N, L));

        if (NdotL > 0.0) {
            // Compute atmospheric transmittance specifically for the sun
            float3 sun_transmittance = ComputeSunTransmittance(world_pos, atmo, L);

            const float3 H = SafeNormalize(V + L);
            const float  NdotH = saturate(dot(N, H));
            const float  VdotH = saturate(dot(V, H));

            const float3 F = FresnelSchlick(VdotH, F0);
            const float  D = DistributionGGX(NdotH, roughness);
            const float  G = GeometrySmith(NdotV, NdotL, roughness);

            const float3 numerator = D * G * F;
            const float  denom = max(4.0 * NdotV * NdotL, 1e-6);
            const float3 specular = numerator / denom;

            const float3 kS = F;
            const float3 kD = (1.0 - kS) * (1.0 - metalness);

            const float3 diffuse = kD * base_rgb;

            const float irradiance = LuxToIrradiance(sun_illuminance);
            const float radiance = irradiance * (1.0 / kPi);
            direct += (diffuse + specular) * sun_color * sun_transmittance * radiance * NdotL;
        }
        sun_rendered = true;
    }

    if (bindless_directional_lights_slot != K_INVALID_BINDLESS_INDEX
        && BX_IN_GLOBAL_SRV(bindless_directional_lights_slot)) {
        StructuredBuffer<DirectionalLightBasic> dir_lights =
            ResourceDescriptorHeap[bindless_directional_lights_slot];

        uint dir_count = 0;
        uint dir_stride = 0;
        dir_lights.GetDimensions(dir_count, dir_stride);

        const uint dir_limit = min(dir_count, MAX_DIRECTIONAL_LIGHTS);
        for (uint i = 0; i < dir_limit; ++i) {
            const DirectionalLightBasic dl = dir_lights[i];
            if ((dl.flags & DIRECTIONAL_LIGHT_FLAG_AFFECTS_WORLD) == 0u) {
                continue;
            }

            // If this light is tagged as a sun light, and we've already rendered the sun
            // via the resolved data, skip it to avoid double lighting.
            const bool is_sun = (dl.flags & DIRECTIONAL_LIGHT_FLAG_SUN_LIGHT) != 0u;
            if (is_sun && sun_rendered) {
                continue;
            }

            // Note: If we have multiple sun lights and HasSunLight() is false (e.g. synthetic sun disabled,
            // but scene lights exist?), then we shouldn't skip them.
            // But HasSunLight() reflects the resolved state. If resolved state says enabled,
            // we render it once efficiently above and skip here.

            const float3 light_dir_ws = -dl.direction_ws;
            const float3 light_color = dl.color_rgb;
            const float  light_intensity = dl.intensity_lux; // Assuming lux for all dir lighting

            const float3 L = SafeNormalize(light_dir_ws);
            const float  NdotL = saturate(dot(N, L));
            if (NdotL <= 0.0) {
                continue;
            }

            float3 transmittance = float3(1.0, 1.0, 1.0);
            const bool env_contribution
                = (dl.flags & DIRECTIONAL_LIGHT_FLAG_ENV_CONTRIBUTION) != 0u;
            // Critical for night correctness:
            // Any directional light that contributes to the environment (or is tagged as sun)
            // should be horizon/atmosphere attenuated. Otherwise lights can leak below horizon.
            if (is_sun || env_contribution) {
                transmittance = ComputeSunTransmittance(world_pos, atmo, L);
            }

            const float3 H = SafeNormalize(V + L);
            const float  NdotH = saturate(dot(N, H));
            const float  VdotH = saturate(dot(V, H));

            const float3 F = FresnelSchlick(VdotH, F0);
            const float  D = DistributionGGX(NdotH, roughness);
            const float  G = GeometrySmith(NdotV, NdotL, roughness);

            const float3 numerator = D * G * F;
            const float  denom = max(4.0 * NdotV * NdotL, 1e-6);
            const float3 specular = numerator / denom;

            const float3 kS = F;
            const float3 kD = (1.0 - kS) * (1.0 - metalness);

            const float3 diffuse = kD * base_rgb;

            const float irradiance = LuxToIrradiance(light_intensity);
            const float radiance = irradiance * (1.0 / kPi);
            direct += (diffuse + specular) * light_color * transmittance * radiance * NdotL;
        }
    }

    return direct;
}

float3 AccumulatePositionalLights(
    float3 world_pos,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    float3 direct = float3(0.0, 0.0, 0.0);

    if (bindless_positional_lights_slot != K_INVALID_BINDLESS_INDEX
        && BX_IN_GLOBAL_SRV(bindless_positional_lights_slot)) {
        StructuredBuffer<PositionalLightData> pos_lights =
            ResourceDescriptorHeap[bindless_positional_lights_slot];

        uint pos_count = 0;
        uint pos_stride = 0;
        pos_lights.GetDimensions(pos_count, pos_stride);

        const uint pos_limit = min(pos_count, MAX_POSITIONAL_LIGHTS);
        for (uint i = 0; i < pos_limit; ++i) {
            const PositionalLightData pl = pos_lights[i];
            if ((pl.flags & POSITIONAL_LIGHT_FLAG_AFFECTS_WORLD) == 0u) {
                continue;
            }

            const float3 to_light = pl.position_ws - world_pos;
            const float dist_sq = dot(to_light, to_light);
            const float range = max(pl.range, 1e-6);
            if (dist_sq >= range * range) {
                continue;
            }

            const float dist = sqrt(dist_sq);
            const float3 L = to_light / max(dist, 1e-6);
            const float NdotL = saturate(dot(N, L));
            if (NdotL <= 0.0) {
                continue;
            }

            float distance_atten = 1.0;
            if (pl.attenuation_model == 0u) { // kInverseSquare
                const float r2 = pl.source_radius * pl.source_radius;
                distance_atten = 1.0 / (dist_sq + r2 + 1e-4);
            } else if (pl.attenuation_model == 1u) { // kLinear
                distance_atten = saturate(1.0 - (dist / range));
            } else { // kCustomExponent
                const float exp = max(pl.decay_exponent, 0.0);
                distance_atten = (exp <= 0.0) ? 1.0 : (1.0 / pow(max(dist, 1e-3), exp));
            }

            const float range_fade = saturate(1.0 - (dist / range));
            if (pl.attenuation_model != 1u) {
                distance_atten *= (range_fade * range_fade);
            }

            float spot_atten = 1.0;
            float normalization = 4.0 * kPi;
            const uint type_bits = (pl.flags & POSITIONAL_LIGHT_TYPE_MASK);
            if (type_bits == POSITIONAL_LIGHT_TYPE_SPOT) {
                const float3 light_dir = SafeNormalize(pl.direction_ws);
                const float3 light_to_p = SafeNormalize(world_pos - pl.position_ws);
                const float cos_theta = dot(light_dir, light_to_p);
                const float inner_cos = pl.inner_cone_cos;
                const float outer_cos = pl.outer_cone_cos;
                const float denom = max(inner_cos - outer_cos, 1e-4);
                spot_atten = saturate((cos_theta - outer_cos) / denom);
                spot_atten *= spot_atten;
                normalization = 2.0 * kPi * (1.0 - pl.outer_cone_cos);
            }

            const float atten = distance_atten * spot_atten;
            if (atten <= 0.0) {
                continue;
            }

            const float3 H = SafeNormalize(V + L);
            const float  NdotH = saturate(dot(N, H));
            const float  VdotH = saturate(dot(V, H));

            const float3 F = FresnelSchlick(VdotH, F0);
            const float  D = DistributionGGX(NdotH, roughness);
            const float  G = GeometrySmith(NdotV, NdotL, roughness);

            const float3 numerator = D * G * F;
            const float  denom = max(4.0 * NdotV * NdotL, 1e-6);
            const float3 specular = numerator / denom;

            const float3 kS = F;
            const float3 kD = (1.0 - kS) * (1.0 - metalness);
            const float3 diffuse = kD * base_rgb;

            const float candela = LumensToCandela(pl.luminous_flux_lm, normalization);
            const float radiance = CandelaToRadiance(candela, atten);
            direct += (diffuse + specular) * pl.color_rgb * radiance * NdotL;
        }
    }

    return direct;
}

// Cluster-Based Implementation
#include "Lighting/ClusterLookup.hlsli"

float3 AccumulatePositionalLightsClustered(
    float3 world_pos,
    float2 screen_pos,
    float  linear_depth,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    const uint cluster_grid_slot = EnvironmentDynamicData.light_culling.bindless_cluster_grid_slot;
    const uint light_list_slot = EnvironmentDynamicData.light_culling.bindless_cluster_index_list_slot;

    const bool has_cluster_data = (cluster_grid_slot != K_INVALID_BINDLESS_INDEX)
                               && (light_list_slot != K_INVALID_BINDLESS_INDEX)
                               && (EnvironmentDynamicData.light_culling.cluster_dim_x > 0);

    if (!has_cluster_data) {
        return AccumulatePositionalLights(world_pos, N, V, NdotV, F0, base_rgb, metalness, roughness);
    }

    const uint cluster_index = GetClusterIndex(screen_pos, linear_depth);
    ClusterLightInfo cluster_info = GetClusterLightInfo(cluster_grid_slot, cluster_index);

    if (cluster_info.light_count == 0) return 0.0.xxx;

    if (bindless_positional_lights_slot == K_INVALID_BINDLESS_INDEX) return 0.0.xxx;

    StructuredBuffer<PositionalLightData> pos_lights = ResourceDescriptorHeap[bindless_positional_lights_slot];
    float3 direct = 0.0.xxx;

    for (uint i = 0; i < cluster_info.light_count; ++i) {
        uint light_index = GetClusterLightIndex(light_list_slot, cluster_info.light_list_offset, i);
        if (light_index == 0xFFFFFFFF) continue;

        const PositionalLightData pl = pos_lights[light_index];
        const float3 to_light = pl.position_ws - world_pos;
        const float dist_sq = dot(to_light, to_light);
        const float range = max(pl.range, 1e-6);
        if (dist_sq >= range * range) continue;

        const float dist = sqrt(dist_sq);
        const float3 L = to_light / max(dist, 1e-6);
        const float NdotL = saturate(dot(N, L));
        if (NdotL <= 0.0) continue;

        float distance_atten = 1.0;
        if (pl.attenuation_model == 0u) {
            distance_atten = 1.0 / (dist_sq + pl.source_radius * pl.source_radius + 1e-4);
        } else if (pl.attenuation_model == 1u) {
            distance_atten = saturate(1.0 - (dist / range));
        } else {
            distance_atten = 1.0 / pow(max(dist, 1e-3), max(pl.decay_exponent, 0.0));
        }

        const float range_fade = saturate(1.0 - (dist / range));
        if (pl.attenuation_model != 1u) distance_atten *= (range_fade * range_fade);

        float spot_atten = 1.0;
        float normalization = 4.0 * kPi;
        if ((pl.flags & POSITIONAL_LIGHT_TYPE_MASK) == POSITIONAL_LIGHT_TYPE_SPOT) {
            const float3 light_dir = SafeNormalize(pl.direction_ws);
            spot_atten = saturate((dot(light_dir, SafeNormalize(world_pos - pl.position_ws)) - pl.outer_cone_cos) / max(pl.inner_cone_cos - pl.outer_cone_cos, 1e-4));
            spot_atten *= spot_atten;
            normalization = 2.0 * kPi * (1.0 - pl.outer_cone_cos);
        }

        const float atten = distance_atten * spot_atten;
        if (atten <= 0.0) continue;

        const float3 H = SafeNormalize(V + L);
        const float3 F = FresnelSchlick(saturate(dot(V, H)), F0);
        const float3 specular = (DistributionGGX(saturate(dot(N, H)), roughness) * GeometrySmith(NdotV, NdotL, roughness) * F) / max(4.0 * NdotV * NdotL, 1e-6);
        const float3 diffuse = (1.0 - F) * (1.0 - metalness) * base_rgb;

        const float candela = LumensToCandela(pl.luminous_flux_lm, normalization);
        const float radiance = CandelaToRadiance(candela, atten);
        direct += (diffuse + specular) * pl.color_rgb * radiance * NdotL;
    }

    return direct;
}

#endif // OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI

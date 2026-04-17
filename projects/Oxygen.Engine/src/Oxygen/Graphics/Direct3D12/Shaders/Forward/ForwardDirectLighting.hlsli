#ifndef OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI
#define OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI

#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Forward/ForwardPbr.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/EnvironmentViewHelpers.hlsli"
#include "Renderer/LightingHelpers.hlsli"
#include "Renderer/ShadowHelpers.hlsli"
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

struct DirectionalLightDiagnosticTerms
{
    float3 full_direct;
    float3 brdf_core;
    float shadow_visibility;
    float transmittance_luma;
    float weight;
};

static inline float ComputePerceptualLuma(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

static inline DirectionalLightDiagnosticTerms EvaluateDirectionalLightDiagnosticTerms(
    DirectionalLightBasic dl,
    float3 world_pos,
    float2 screen_position_xy,
    GpuSkyAtmosphereParams atmo,
    float3 shadow_normal_ws,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    DirectionalLightDiagnosticTerms terms = (DirectionalLightDiagnosticTerms)0;

    if ((dl.flags & DIRECTIONAL_LIGHT_FLAG_AFFECTS_WORLD) == 0u) {
        return terms;
    }

    const float3 light_dir_ws = -dl.direction_ws;
    const float3 L = SafeNormalize(light_dir_ws);
    if (dot(L, L) < 0.5) {
        return terms;
    }

    const float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0) {
        return terms;
    }
    terms.weight = NdotL;

    float3 transmittance = 1.0.xxx;
    const bool is_sun = (dl.flags & DIRECTIONAL_LIGHT_FLAG_SUN_LIGHT) != 0u;
    const bool env_contribution =
        (dl.flags & DIRECTIONAL_LIGHT_FLAG_ENV_CONTRIBUTION) != 0u;
    if (is_sun || env_contribution) {
        transmittance = saturate(ComputeSunTransmittance(world_pos, atmo, L));
    }
    terms.transmittance_luma = ComputePerceptualLuma(transmittance);

    terms.shadow_visibility = saturate(ComputeShadowVisibility(
        dl.shadow_index, world_pos, screen_position_xy, shadow_normal_ws, L));

    const float3 H_unorm = V + L;
    const float H_len_sq = dot(H_unorm, H_unorm);
    const float3 H = H_len_sq > 1.0e-8 ? H_unorm * rsqrt(H_len_sq) : N;
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));

    const float3 F = FresnelSchlick(VdotH, F0);
    const float D = DistributionGGX(NdotH, roughness);
    const float G = GeometrySmith(NdotV, NdotL, roughness);

    const float3 numerator = D * G * F;
    const float denom = max(4.0 * NdotV * NdotL, 1.0e-6);
    const float3 specular = numerator / denom;

    const float3 kS = F;
    const float3 kD = (1.0 - kS) * (1.0 - metalness);
    const float3 diffuse = kD * base_rgb;
    terms.brdf_core = (diffuse + specular) * NdotL;

    const float irradiance = LuxToIrradiance(dl.intensity_lux);
    const float radiance = irradiance * (1.0 / kPi);

    terms.full_direct = terms.brdf_core * dl.color_rgb * transmittance * radiance
        * terms.shadow_visibility;
    return terms;
}

static inline float3 EvaluateDirectionalLightContribution(
    DirectionalLightBasic dl,
    float3 world_pos,
    float2 screen_position_xy,
    GpuSkyAtmosphereParams atmo,
    float3 shadow_normal_ws,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    return EvaluateDirectionalLightDiagnosticTerms(
        dl, world_pos, screen_position_xy, atmo, shadow_normal_ws, N, V, NdotV, F0, base_rgb,
        metalness, roughness).full_direct;
}

static inline float3 EvaluateDirectionalLightContributionRawLambert(
    DirectionalLightBasic dl,
    float3 N,
    float3 base_rgb)
{
    if ((dl.flags & DIRECTIONAL_LIGHT_FLAG_AFFECTS_WORLD) == 0u) {
        return 0.0.xxx;
    }

    const float3 L = SafeNormalize(-dl.direction_ws);
    if (dot(L, L) < 0.5) {
        return 0.0.xxx;
    }

    const float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0) {
        return 0.0.xxx;
    }

    const float radiance = LuxToIrradiance(dl.intensity_lux) * (1.0 / kPi);
    return base_rgb * dl.color_rgb * radiance * NdotL;
}

float3 AccumulateDirectionalLightsRawLambert(
    float3 N,
    float3 base_rgb)
{
    float3 direct = 0.0.xxx;
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    if (lighting.directional_lights_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(lighting.directional_lights_slot)) {
        return direct;
    }

    StructuredBuffer<DirectionalLightBasic> dir_lights =
        ResourceDescriptorHeap[lighting.directional_lights_slot];
    uint dir_count = 0u;
    uint dir_stride = 0u;
    dir_lights.GetDimensions(dir_count, dir_stride);

    const uint dir_limit = min(dir_count, MAX_DIRECTIONAL_LIGHTS);
    for (uint i = 0; i < dir_limit; ++i) {
        direct += EvaluateDirectionalLightContributionRawLambert(
            dir_lights[i], N, base_rgb);
    }

    return direct;
}

float3 AccumulateDirectionalLightGatesDebug(
    float3 world_pos,
    float2 screen_position_xy,
    GpuSkyAtmosphereParams atmo,
    float3 shadow_normal_ws,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    float shadow_sum = 0.0;
    float trans_sum = 0.0;
    float weight_sum = 0.0;

    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    if (lighting.directional_lights_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(lighting.directional_lights_slot)) {
        return 0.0.xxx;
    }

    StructuredBuffer<DirectionalLightBasic> dir_lights =
        ResourceDescriptorHeap[lighting.directional_lights_slot];
    uint dir_count = 0u;
    uint dir_stride = 0u;
    dir_lights.GetDimensions(dir_count, dir_stride);

    const uint dir_limit = min(dir_count, MAX_DIRECTIONAL_LIGHTS);
    for (uint i = 0; i < dir_limit; ++i) {
        const DirectionalLightDiagnosticTerms terms =
            EvaluateDirectionalLightDiagnosticTerms(
                dir_lights[i], world_pos, screen_position_xy, atmo, shadow_normal_ws, N, V, NdotV,
                F0, base_rgb, metalness, roughness);
        shadow_sum += terms.shadow_visibility * terms.weight;
        trans_sum += terms.transmittance_luma * terms.weight;
        weight_sum += terms.weight;
    }

    if (weight_sum <= 1.0e-6) {
        return 0.0.xxx;
    }

    return float3(shadow_sum / weight_sum, trans_sum / weight_sum, 0.0);
}

float3 AccumulateDirectionalLightsBrdfCore(
    float3 world_pos,
    float2 screen_position_xy,
    GpuSkyAtmosphereParams atmo,
    float3 shadow_normal_ws,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    float3 brdf_core = 0.0.xxx;
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    if (lighting.directional_lights_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(lighting.directional_lights_slot)) {
        return brdf_core;
    }

    StructuredBuffer<DirectionalLightBasic> dir_lights =
        ResourceDescriptorHeap[lighting.directional_lights_slot];
    uint dir_count = 0u;
    uint dir_stride = 0u;
    dir_lights.GetDimensions(dir_count, dir_stride);

    const uint dir_limit = min(dir_count, MAX_DIRECTIONAL_LIGHTS);
    for (uint i = 0; i < dir_limit; ++i) {
        const DirectionalLightDiagnosticTerms terms =
            EvaluateDirectionalLightDiagnosticTerms(
                dir_lights[i], world_pos, screen_position_xy, atmo, shadow_normal_ws, N, V, NdotV,
                F0, base_rgb, metalness, roughness);
        brdf_core += terms.brdf_core * dir_lights[i].color_rgb;
    }

    return brdf_core;
}

float3 AccumulateDirectionalLights(
    float3 world_pos,
    float2 screen_position_xy,
    GpuSkyAtmosphereParams atmo,
    float3 shadow_normal_ws,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    float3 direct = float3(0.0, 0.0, 0.0);
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    if (lighting.directional_lights_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(lighting.directional_lights_slot)) {
        return direct;
    }
    StructuredBuffer<DirectionalLightBasic> dir_lights =
        ResourceDescriptorHeap[lighting.directional_lights_slot];
    uint dir_count = 0;
    uint dir_stride = 0;
    dir_lights.GetDimensions(dir_count, dir_stride);

    // Direct surface lighting comes only from the directional-light buffer.
    // The resolved sun payload remains an atmosphere/sky input, not a second
    // authoritative source of direct surface illumination.
    const uint dir_limit = min(dir_count, MAX_DIRECTIONAL_LIGHTS);
    for (uint i = 0; i < dir_limit; ++i) {
        direct += EvaluateDirectionalLightContribution(
            dir_lights[i], world_pos, screen_position_xy, atmo, shadow_normal_ws, N, V, NdotV,
            F0, base_rgb, metalness, roughness);
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

    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();

    if (lighting.positional_lights_slot != K_INVALID_BINDLESS_INDEX
        && BX_IN_GLOBAL_SRV(lighting.positional_lights_slot)) {
        StructuredBuffer<PositionalLightData> pos_lights =
            ResourceDescriptorHeap[lighting.positional_lights_slot];

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
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    const uint cluster_grid_slot = lighting.grid_indirection_srv;
    const uint light_list_slot = lighting.light_view_data_srv;

    const bool has_cluster_data = (cluster_grid_slot != K_INVALID_BINDLESS_INDEX)
                               && (light_list_slot != K_INVALID_BINDLESS_INDEX)
                               && (lighting.grid_size.x > 0);

    if (!has_cluster_data) {
        return AccumulatePositionalLights(world_pos, N, V, NdotV, F0, base_rgb, metalness, roughness);
    }

    const uint cluster_index = GetClusterIndex(screen_pos, linear_depth);
    ClusterLightInfo cluster_info = GetClusterLightInfo(cluster_grid_slot, cluster_index);

    if (cluster_info.light_count == 0) return 0.0.xxx;

    if (lighting.positional_lights_slot == K_INVALID_BINDLESS_INDEX) return 0.0.xxx;

    StructuredBuffer<PositionalLightData> pos_lights = ResourceDescriptorHeap[lighting.positional_lights_slot];
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

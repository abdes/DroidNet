//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI
#define OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI

#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Passes/Forward/ForwardPbr.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Common/PhysicalLighting.hlsli"

// Safety caps for fallback loops (pre-clustered culling).
#ifndef MAX_DIRECTIONAL_LIGHTS
#define MAX_DIRECTIONAL_LIGHTS 16
#endif

#ifndef MAX_POSITIONAL_LIGHTS
#define MAX_POSITIONAL_LIGHTS 1024
#endif

float3 AccumulateDirectionalLights(
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    float3 direct = float3(0.0, 0.0, 0.0);

    // Check if sun override is enabled
    const bool use_sun_override = IsOverrideSunEnabled();
    const float3 override_sun_dir = GetOverrideSunDirectionWS();
    const float  override_sun_illum = GetOverrideSunIlluminance();
    const float3 override_sun_color = GetSunColorRGB();
    const float  override_sun_intensity = GetSunIntensity();
    bool found_sun = false;

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

            // Check if this is the sun light and we have an override
            const bool is_sun = (dl.flags & DIRECTIONAL_LIGHT_FLAG_SUN_LIGHT) != 0u;
            if (is_sun) {
                found_sun = true;
            }

            float3 light_dir_ws;
            float  light_intensity;
            float3 light_color;

            if (is_sun && use_sun_override) {
                light_dir_ws = override_sun_dir;
                light_intensity = override_sun_illum;
                light_color = dl.color_rgb;
            } else {
                light_dir_ws = -dl.direction_ws;
                light_intensity = dl.intensity_lux;
                light_color = dl.color_rgb;
            }

            const float3 L = SafeNormalize(light_dir_ws);
            const float  NdotL = saturate(dot(N, L));
            if (NdotL <= 0.0) {
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

            const float irradiance = LuxToIrradiance(light_intensity);
            const float radiance = irradiance * (1.0 / kPi);
            direct += (diffuse + specular) * light_color * radiance * NdotL;
        }

        if (use_sun_override && !found_sun) {
            const float3 L = SafeNormalize(override_sun_dir);
            const float  NdotL = saturate(dot(N, L));
            if (NdotL > 0.0) {
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

                const float irradiance = LuxToIrradiance(override_sun_intensity);
                const float radiance = irradiance * (1.0 / kPi);
                direct += (diffuse + specular) * override_sun_color
                          * radiance * NdotL;
            }
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
#include "Passes/Lighting/ClusterLookup.hlsli"

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
    const uint cluster_grid_slot = EnvironmentDynamicData.bindless_cluster_grid_slot;
    const uint light_list_slot = EnvironmentDynamicData.bindless_cluster_index_list_slot;

    const bool has_cluster_data = (cluster_grid_slot != K_INVALID_BINDLESS_INDEX)
                               && (light_list_slot != K_INVALID_BINDLESS_INDEX)
                               && (EnvironmentDynamicData.cluster_dim_x > 0);

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

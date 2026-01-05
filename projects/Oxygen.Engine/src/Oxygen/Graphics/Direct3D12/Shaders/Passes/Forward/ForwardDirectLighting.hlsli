#ifndef OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI
#define OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI

#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Passes/Forward/ForwardPbr.hlsli"

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

            // Scene publishes dl.direction_ws as incoming ray direction
            // (from light toward the scene). Shading expects surface->light.
            const float3 L = SafeNormalize(-dl.direction_ws);
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
            const float3 diffuse = kD * base_rgb / kPi;

            direct += (diffuse + specular) * dl.color_rgb * dl.intensity * NdotL;
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

            // Distance attenuation based on authored model.
            float distance_atten = 1.0;
            if (pl.attenuation_model == 0u) { // kInverseSquare
                distance_atten = 1.0 / max(dist_sq, 1e-4);
            } else if (pl.attenuation_model == 1u) { // kLinear
                distance_atten = saturate(1.0 - (dist / range));
            } else { // kCustomExponent
                distance_atten = pow(saturate(1.0 - (dist / range)),
                                     max(pl.decay_exponent, 0.0));
            }

            // Optional smoothing near range edge to reduce harsh cutoffs.
            const float range_fade = saturate(1.0 - (dist / range));
            distance_atten *= (range_fade * range_fade);

            // Spot cone attenuation.
            float spot_atten = 1.0;
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
            const float3 diffuse = kD * base_rgb / kPi;

            direct += (diffuse + specular) * pl.color_rgb * pl.intensity * (NdotL * atten);
        }
    }

    return direct;
}

#endif // OXYGEN_PASSES_FORWARD_FORWARDDIRECTLIGHTING_HLSLI

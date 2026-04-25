#ifndef OXYGEN_VORTEX_SERVICES_LIGHTING_FORWARDDIRECTLIGHTING_HLSLI
#define OXYGEN_VORTEX_SERVICES_LIGHTING_FORWARDDIRECTLIGHTING_HLSLI

#include "Vortex/Contracts/Lighting/PositionalLightData.hlsli"
#include "Vortex/Stages/Translucency/ForwardPbr.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Contracts/Shadows/ShadowHelpers.hlsli"
#include "Vortex/Shared/Lighting.hlsli"
#include "Vortex/Shared/Geometry.hlsli"
#include "Vortex/Services/Lighting/AtmosphereDirectionalLightShared.hlsli"

#ifndef MAX_POSITIONAL_LIGHTS
#define MAX_POSITIONAL_LIGHTS 1024
#endif

static const uint kDirectionalLightFlagAffectsWorld = 1u << 0u;

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

static inline bool TryGetResolvedDirectionalLight(out DirectionalLightForwardData directional_light)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    directional_light = lighting.directional;
    return lighting.has_directional_light != 0u
        && (directional_light.light_flags & kDirectionalLightFlagAffectsWorld) != 0u;
}

static inline DirectionalLightDiagnosticTerms EvaluateDirectionalLightDiagnosticTerms(
    DirectionalLightForwardData dl,
    float3 world_pos,
    float2 screen_position_xy,
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

    const float3 L = SafeNormalize(dl.direction);
    if (dot(L, L) < 0.5) {
        return terms;
    }

    const float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0) {
        return terms;
    }
    terms.weight = NdotL;

    const float3 transmittance = ResolveDirectionalLightAtmosphereTransmittance(
        world_pos,
        dl.direction,
        dl.transmittance_toward_sun_rgb,
        dl.atmosphere_mode_flags);
    terms.transmittance_luma = ComputePerceptualLuma(transmittance);

    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    terms.shadow_visibility = saturate(ComputeShadowVisibility(
        shadow_bindings.sun_shadow_index, world_pos, screen_position_xy, shadow_normal_ws, L));

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

    const float irradiance = LuxToIrradiance(dl.illuminance_lux);
    const float3 raw_radiance = dl.color * (irradiance * (1.0 / kPi));
    terms.full_direct = terms.brdf_core
        * ResolveDirectionalLightAtmosphereRadiance(
            world_pos,
            dl.direction,
            dl.transmittance_toward_sun_rgb,
            dl.atmosphere_mode_flags,
            raw_radiance)
        * terms.shadow_visibility;
    return terms;
}

static inline float3 EvaluateDirectionalLightContribution(
    DirectionalLightForwardData dl,
    float3 world_pos,
    float2 screen_position_xy,
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
        dl, world_pos, screen_position_xy, shadow_normal_ws, N, V, NdotV, F0, base_rgb,
        metalness, roughness).full_direct;
}

static inline float3 EvaluateDirectionalLightContributionRawLambert(
    DirectionalLightForwardData dl,
    float3 N,
    float3 base_rgb)
{
    const float3 L = SafeNormalize(dl.direction);
    if (dot(L, L) < 0.5) {
        return 0.0.xxx;
    }

    const float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0) {
        return 0.0.xxx;
    }

    const float radiance = LuxToIrradiance(dl.illuminance_lux) * (1.0 / kPi);
    return base_rgb * dl.color * radiance * NdotL;
}

float3 AccumulateDirectionalLightsRawLambert(
    float3 N,
    float3 base_rgb)
{
    DirectionalLightForwardData directional_light = (DirectionalLightForwardData)0;
    if (!TryGetResolvedDirectionalLight(directional_light)) {
        return 0.0.xxx;
    }

    return EvaluateDirectionalLightContributionRawLambert(
        directional_light, N, base_rgb);
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
    (void)atmo;

    DirectionalLightForwardData directional_light = (DirectionalLightForwardData)0;
    if (!TryGetResolvedDirectionalLight(directional_light)) {
        return 0.0.xxx;
    }

    const DirectionalLightDiagnosticTerms terms =
        EvaluateDirectionalLightDiagnosticTerms(
            directional_light, world_pos, screen_position_xy, shadow_normal_ws, N, V, NdotV,
            F0, base_rgb, metalness, roughness);
    return float3(terms.shadow_visibility, terms.transmittance_luma, 0.0);
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
    (void)atmo;

    DirectionalLightForwardData directional_light = (DirectionalLightForwardData)0;
    if (!TryGetResolvedDirectionalLight(directional_light)) {
        return 0.0.xxx;
    }

    const DirectionalLightDiagnosticTerms terms =
        EvaluateDirectionalLightDiagnosticTerms(
            directional_light, world_pos, screen_position_xy, shadow_normal_ws, N, V, NdotV,
            F0, base_rgb, metalness, roughness);
    return terms.brdf_core * directional_light.color;
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
    (void)atmo;

    DirectionalLightForwardData directional_light = (DirectionalLightForwardData)0;
    if (!TryGetResolvedDirectionalLight(directional_light)) {
        return 0.0.xxx;
    }

    return EvaluateDirectionalLightContribution(
        directional_light, world_pos, screen_position_xy, shadow_normal_ws, N, V, NdotV,
        F0, base_rgb, metalness, roughness);
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

            float atten = saturate(1.0 - (dist / range));
            atten *= atten;

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
            direct += (diffuse + specular) * pl.color_rgb * pl.luminous_flux_lm * atten * NdotL;
        }
    }

    return direct;
}

float3 AccumulatePositionalLightsClustered(
    float3 world_pos,
    float2 screen_position_xy,
    float linear_depth,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness)
{
    (void)screen_position_xy;
    (void)linear_depth;
    return AccumulatePositionalLights(
        world_pos, N, V, NdotV, F0, base_rgb, metalness, roughness);
}

#endif

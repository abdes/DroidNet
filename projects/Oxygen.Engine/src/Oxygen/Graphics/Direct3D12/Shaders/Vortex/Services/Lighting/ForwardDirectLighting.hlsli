#ifndef OXYGEN_VORTEX_SERVICES_LIGHTING_FORWARDDIRECTLIGHTING_HLSLI
#define OXYGEN_VORTEX_SERVICES_LIGHTING_FORWARDDIRECTLIGHTING_HLSLI

#include "Vortex/Contracts/Lighting/ForwardLocalLightRecord.hlsli"
#include "Vortex/Contracts/View/HdrConsumerInputs.hlsli"
#include "Vortex/Stages/Translucency/ForwardPbr.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Vortex/Services/Shadows/ShadowSurfaceNormal.hlsli"
#include "Vortex/Services/Shadows/ContactShadow.hlsli"
#include "Vortex/Shared/Lighting.hlsli"
#include "Vortex/Services/Lighting/FiniteEmitter.hlsli"
#include "Vortex/Shared/Geometry.hlsli"
#include "Vortex/Services/Lighting/AtmosphereDirectionalLightShared.hlsli"


static void RecordForwardHdrSource(float3 scene_rgb)
{
#if !defined(OXYGEN_OPAQUE_OUTPUT) || defined(OXYGEN_DEPTH_COMPLETE)
    RecordHdrSceneSource(scene_rgb, 4u);
#endif
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
    float  roughness,
    bool receives_shadows)
{
    DirectionalLightDiagnosticTerms terms = (DirectionalLightDiagnosticTerms)0;

    const float3 L = SafeNormalize(dl.direction_to_source_ws);
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
        dl.direction_to_source_ws,
        dl.ground_transmittance_rgb,
        dl.atmosphere_mode_flags);
    terms.transmittance_luma = ComputePerceptualLuma(transmittance);

    terms.shadow_visibility = receives_shadows
        ? saturate(ComputeDirectionalShadowVisibility(
            dl.selection_index, world_pos, shadow_normal_ws, L))
            * ComputeContactShadowVisibility(dl.flags, true, world_pos, shadow_normal_ws, L) : 1.0f;

    terms.brdf_core = EvaluateGgxDirectResponse(N, V, L, F0, base_rgb * (1.0 - metalness),
        roughness, LoadResolvedLightingFrameBindings());

    const float3 raw_radiance = dl.illuminance_rgb_lux;
    terms.full_direct = terms.brdf_core
        * ResolveDirectionalLightAtmosphereRadiance(
            world_pos,
            dl.direction_to_source_ws,
            dl.ground_transmittance_rgb,
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
    float  roughness,
    bool receives_shadows)
{
    const float3 radiance = EvaluateDirectionalLightDiagnosticTerms(
        dl, world_pos, screen_position_xy, shadow_normal_ws, N, V, NdotV, F0, base_rgb,
        metalness, roughness, receives_shadows).full_direct;
    RecordForwardHdrSource(radiance);
    return radiance;
}

static inline float3 EvaluateDirectionalLightContributionRawLambert(
    DirectionalLightForwardData dl,
    float3 N,
    float3 base_rgb)
{
    const float3 L = SafeNormalize(dl.direction_to_source_ws);
    if (dot(L, L) < 0.5) {
        return 0.0.xxx;
    }

    const float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0) {
        return 0.0.xxx;
    }

    return base_rgb * dl.illuminance_rgb_lux * (1.0 / PI) * NdotL;
}

float3 AccumulateDirectionalLightsRawLambert(
    float3 N,
    float3 base_rgb)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    float3 result = 0.0.xxx;
    for (uint index = 0; index < lighting.directional_count; ++index) {
        DirectionalLightForwardData light;
        if (TryLoadDirectionalLight(lighting, index, light))
            result += EvaluateDirectionalLightContributionRawLambert(light, N, base_rgb);
    }
    return result;
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
    float  roughness,
    bool receives_shadows)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    float3 result = 0.0.xxx;
    float count = 0.0;
    for (uint index = 0; index < lighting.directional_count; ++index) {
        DirectionalLightForwardData light;
        if (!TryLoadDirectionalLight(lighting, index, light)) continue;
        const DirectionalLightDiagnosticTerms terms = EvaluateDirectionalLightDiagnosticTerms(
            light, world_pos, screen_position_xy, shadow_normal_ws, N, V, NdotV, F0, base_rgb, metalness, roughness, receives_shadows);
        result += float3(terms.shadow_visibility, terms.transmittance_luma, 0.0);
        count += 1.0;
    }
    return count > 0.0 ? result / count : result;
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
    float  roughness,
    bool receives_shadows)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    float3 result = 0.0.xxx;
    for (uint index = 0; index < lighting.directional_count; ++index) {
        DirectionalLightForwardData light;
        if (!TryLoadDirectionalLight(lighting, index, light)) continue;
        result += EvaluateDirectionalLightDiagnosticTerms(light, world_pos, screen_position_xy,
            shadow_normal_ws, N, V, NdotV, F0, base_rgb, metalness, roughness, receives_shadows).brdf_core;
    }
    return result;
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
    float  roughness,
    bool receives_shadows)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    float3 result = 0.0.xxx;
    for (uint index = 0; index < lighting.directional_count; ++index) {
        DirectionalLightForwardData light;
        if (TryLoadDirectionalLight(lighting, index, light))
            result += EvaluateDirectionalLightContribution(light, world_pos, screen_position_xy,
                shadow_normal_ws, N, V, NdotV, F0, base_rgb, metalness, roughness, receives_shadows);
    }
    return result;
}

float3 AccumulateLocalLightsClustered(
    float3 world_pos,
    float2 screen_position_xy,
    float linear_depth,
    float3 shadow_normal_ws,
    float3 N,
    float3 V,
    float  NdotV,
    float3 F0,
    float3 base_rgb,
    float  metalness,
    float  roughness,
    bool receives_shadows)
{
    float3 direct = float3(0.0, 0.0, 0.0);

    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();

    if (IsLightingPublicationReady(lighting) && BX_IN_GLOBAL_SRV(lighting.local_records_srv)
        && BX_IN_GLOBAL_SRV(lighting.cluster_ranges_srv)
        && BX_IN_GLOBAL_SRV(lighting.grid_metadata_srv)
        && lighting.local_count > 0u) {
        StructuredBuffer<ForwardLocalLightRecord> local_lights =
            ResourceDescriptorHeap[lighting.local_records_srv];

        uint record_count = 0u, record_stride = 0u;
        local_lights.GetDimensions(record_count, record_stride);
        if (record_count < lighting.local_count) {
            return direct;
        }
        const uint record_limit = lighting.local_count;
        const LightGridMetadata grid = LoadLightGridMetadata(lighting.grid_metadata_srv);
        if (any(grid.grid_size == 0u) || any(grid.content_extent_px <= 0.0f)) {
            return direct;
        }
        const uint cluster = ComputeClusterIndex(screen_position_xy, linear_depth, grid);
        const ClusterLightRange range = GetClusterLightRange(lighting.cluster_ranges_srv, cluster);
        ClusterLightIteration iteration;
        if (!TryResolveClusterLightIteration(range, record_limit,
                lighting.local_indices_srv, iteration)) {
            return direct;
        }
        const GgxDirectContext brdf = PrepareGgxDirect(N, V, F0,
            base_rgb * (1.0 - metalness), roughness, lighting);
        for (uint i = 0; i < iteration.count; ++i) {
            const uint light_index = LoadClusterLightIndex(iteration, i);
            if (light_index >= record_limit) {
                continue;
            }
            const ForwardLocalLightRecord light = local_lights[light_index];
            const uint kind = light.kind;
            if (kind != FORWARD_LOCAL_LIGHT_POINT && kind != FORWARD_LOCAL_LIGHT_SPOT) {
                continue;
            }

            LocalEmitterInput source;
            if (!PrepareLocalEmitterInput(light, world_pos, source)
                || !LocalEmitterFacesSurface(light, source, N)) continue;
            const LightShadowReference shadow = LoadLightShadowReference(
                lighting.local_shadow_map_srv, light.selection_index);
            const float visibility = receives_shadows
                ? ComputeLocalShadowVisibility(shadow,
                    world_pos, N, source.direction_to_center)
                    * ComputeContactShadowVisibility(light.flags, true, world_pos,
                        shadow_normal_ws, source.direction_to_center) : 1.0f;
            if (visibility <= 0.0) continue;
            const GgxDirectLobes lobes = EvaluatePreparedLocalEmitterLobes(
                light, source, N, V, brdf);
            const float3 contribution = (lobes.single_scattering
                + lobes.multiple_scattering + lobes.diffuse) * visibility;
            RecordForwardHdrSource(contribution);
            direct += contribution;
        }
    }

    return direct;
}

#endif

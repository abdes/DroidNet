//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Vortex/Services/Lighting/AtmosphereDirectionalLightShared.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint DEFERRED_LIGHT_TYPE_STATIC_SKY_LIGHT = 3u;

[shader("vertex")]
VortexFullscreenTriangleOutput DeferredLightDirectionalVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 DeferredLightDirectionalPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    if (g_PassConstantsIndex == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxxx;
    }

    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    const SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
    if (!HasDeferredLightingInputs(bindings)) return 0.0f.xxxx;
    const float scene_depth = SampleSceneDepth(input.uv, bindings);
    if (IsDeferredBackgroundDepth(scene_depth)) return 0.0f.xxxx;
    const float3 world_position =
        ReconstructDeferredWorldPosition(input.uv, scene_depth);
    const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(
        input.uv, world_position, camera_position, bindings);
    if (light_constants.light_type == DEFERRED_LIGHT_TYPE_STATIC_SKY_LIGHT) {
        const float3 diffuse = EvaluateDeferredStaticSkyLightDiffuse(surface);
        RecordHdrSceneSource(diffuse, 3u);
        return float4(diffuse * GetPreExposure(), 0.0f);
    }

#if defined(DEBUG_IBL_ONLY)
    return 0.0f.xxxx;
#endif

    const LightingFrameBindings lighting_bindings = LoadResolvedLightingFrameBindings();
    const GgxDirectContext brdf = PrepareGgxDirect(surface.world_normal,
        surface.view_direction, surface.specular_f0,
        surface.base_color * (1.0 - surface.metallic), surface.roughness, lighting_bindings);
    float3 accumulated = 0.0.xxx;
    // Directional sources cover the same pixels. Decode the surface and prepare
    // its view/material terms once; retain each source's visibility and transport.
    [loop] for (uint index = 0u; index < lighting_bindings.directional_count; ++index) {
        DirectionalLightForwardData light;
        if (!TryLoadDirectionalLight(lighting_bindings, index, light)) continue;
        const float3 light_dir = light.direction_to_source_ws;
        const LightShadowReference shadow_reference = LoadLightShadowReference(
            lighting_bindings.directional_shadow_map_srv, light.selection_index);
        float visibility = 1.0;
        if (shadow_reference.projection_kind == SHADOW_PROJECTION_CASCADED_2D) {
            visibility = ComputeDirectionalShadowVisibility(light.selection_index,
                world_position, surface.world_normal, light_dir);
        }
        const float3 radiance = ResolveDirectionalLightAtmosphereRadiance(
            world_position, light_dir, light.ground_transmittance_rgb,
            light.atmosphere_mode_flags, light.illuminance_rgb_lux);
#if defined(DEBUG_DIRECT_LIGHTING_ONLY)
        accumulated += surface.base_color * radiance * saturate(dot(surface.world_normal, light_dir));
#elif defined(DEBUG_DIRECT_LIGHT_GATES)
        accumulated += float3(saturate(visibility), dot(saturate(light.ground_transmittance_rgb),
            float3(0.2126, 0.7152, 0.0722)), 0.0);
#else
        const GgxDirectLobes lobes = EvaluatePreparedGgxDirectLobes(surface.world_normal,
            surface.view_direction, normalize(light_dir), brdf);
        const float3 response = lobes.single_scattering + lobes.multiple_scattering + lobes.diffuse;
#if defined(DEBUG_DIRECT_BRDF_CORE)
        accumulated += response;
#else
        const float3 contribution = response * radiance * visibility;
        RecordHdrSceneSource(contribution, 2u);
        accumulated += contribution;
#endif
#endif
    }
#if defined(DEBUG_DIRECT_LIGHT_GATES)
    return float4(accumulated, 0.0);
#else
    return float4(accumulated * GetPreExposure(), 0.0);
#endif
}

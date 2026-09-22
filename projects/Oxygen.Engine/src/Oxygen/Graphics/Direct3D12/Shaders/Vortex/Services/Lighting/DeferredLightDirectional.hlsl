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
    const float scene_depth = SampleSceneDepth(input.uv, bindings);
    const float3 world_position =
        ReconstructDeferredWorldPosition(input.uv, scene_depth);
    const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(
        input.uv, world_position, camera_position, bindings);
    if (light_constants.light_type == DEFERRED_LIGHT_TYPE_STATIC_SKY_LIGHT) {
        if (!HasDeferredLightingInputs(bindings) || scene_depth >= 1.0f) {
            return 0.0f.xxxx;
        }
        const float3 diffuse = EvaluateDeferredStaticSkyLightDiffuse(surface);
        RecordHdrSceneSource(diffuse, 3u);
        return float4(diffuse * GetPreExposure(), 0.0f);
    }

#if defined(DEBUG_IBL_ONLY)
    return 0.0f.xxxx;
#endif

    const LightingFrameBindings lighting_bindings = LoadResolvedLightingFrameBindings();
    DirectionalLightForwardData light;
    if (!TryLoadDirectionalLight(lighting_bindings, light_constants.selection_index, light)) return 0.0f.xxxx;
    const LightShadowReference shadow_reference = LoadLightShadowReference(
        lighting_bindings.directional_shadow_map_srv, light.selection_index);
    const float3 light_dir =
        light.direction_to_source_ws;
    float light_attenuation = 1.0f;
    if (shadow_reference.projection_kind == SHADOW_PROJECTION_CASCADED_2D) {
        light_attenuation = ComputeDirectionalShadowVisibility(
            light.selection_index, world_position,
            surface.world_normal,
            light_dir);
    }
    const float3 deferred_light_radiance = ResolveDirectionalLightAtmosphereRadiance(
        world_position,
        light_dir,
        light.ground_transmittance_rgb,
        light.atmosphere_mode_flags,
        light.illuminance_rgb_lux);
#if defined(DEBUG_DIRECT_LIGHTING_ONLY)
    const float NoL = saturate(dot(surface.world_normal, light_dir));
    return float4(surface.base_color * deferred_light_radiance * NoL * GetPreExposure(), 0.0f);
#elif defined(DEBUG_DIRECT_LIGHT_GATES)
    const float transmittance_luma = dot(
        saturate(light.ground_transmittance_rgb),
        float3(0.2126f, 0.7152f, 0.0722f));
    return float4(saturate(light_attenuation), saturate(transmittance_luma), 0.0f, 0.0f);
#elif defined(DEBUG_DIRECT_BRDF_CORE)
    return float4(EvaluateCookTorranceLighting(surface, light_dir, 1.0f.xxx) * GetPreExposure(), 0.0f);
#endif
    const float3 lighting = EvaluateDeferredLightAtWorldPosition(
        input.uv,
        scene_depth,
        world_position,
        light_dir,
        deferred_light_radiance,
        light_attenuation,
        camera_position,
        bindings);
    RecordHdrSceneSource(lighting, 2u);
    return float4(lighting * GetPreExposure(), 0.0f);
}

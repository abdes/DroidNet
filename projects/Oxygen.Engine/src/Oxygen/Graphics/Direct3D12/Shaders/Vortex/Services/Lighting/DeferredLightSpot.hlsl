//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"
#include "Vortex/Services/Lighting/FiniteEmitter.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// The source disk swept along its cone has radius R + h*tan(theta).
// Reject outside that support before G-buffer reads and cube-shadow filtering.
// Slack keeps rejection conservative; source integration still owns radiance.
static bool SpotEmitterHasSpatialSupport(ForwardLocalLightRecord light, float3 receiver)
{
    const float3 ray = light.position_ws - receiver;
    const float distance_squared = dot(ray, ray);
    const float extent = light.range_m + light.source_radius_m;
    if (light.range_m <= 0.0 || all(light.intensity_rgb_cd == 0.0)
        || distance_squared >= extent * extent) return false;
    const float3 axis = normalize(light.emitted_direction_ws);
    const float height = -dot(ray, axis);
    const float slack = 2.0e-6 * (sqrt(distance_squared) + light.source_radius_m);
    if (height < -slack) return false;
    const float s = light.outer_cone_sin_half_squared
        * (1.0 + light.outer_cone_relative_correction);
    const float cosine = 1.0 - 2.0 * s;
    const float sine = 2.0 * sqrt(s * (1.0 - s));
    const float transverse = max(0.0, length(cross(ray, axis)) - light.source_radius_m);
    return cosine <= 0.0 || transverse * cosine <= max(0.0, height) * sine + slack;
}

[shader("vertex")]
DeferredLightVolumeVSOutput DeferredLightSpotVS(uint vertex_id : SV_VertexID)
{
    DeferredLightVolumeVSOutput output = (DeferredLightVolumeVSOutput)0;
    if (g_PassConstantsIndex == INVALID_BINDLESS_INDEX) {
        return output;
    }

    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return GenerateDeferredLightVolume(
        LoadDeferredLightGeometryVertex(
            light_constants.light_geometry_vertices_srv,
            vertex_id),
        light_constants.light_world_matrix);
}

[shader("pixel")]
float4 DeferredLightSpotPS(DeferredLightVolumeVSOutput input) : SV_Target0
{
    if (g_PassConstantsIndex == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxxx;
    }

    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    const SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
    if (!HasDeferredLightingInputs(bindings)) {
        return 0.0f.xxxx;
    }

    const float2 screen_uv = ResolveDeferredLightScreenUv(input.screen_position);
    const float scene_depth = SampleSceneDepth(screen_uv, bindings);
    if (IsDeferredBackgroundDepth(scene_depth)) return 0.0f.xxxx;
    const float3 world_position
        = ReconstructDeferredWorldPosition(screen_uv, scene_depth);
    const LightingFrameBindings lighting_bindings = LoadResolvedLightingFrameBindings();
    ForwardLocalLightRecord light;
    if (!TryLoadLocalLight(lighting_bindings, light_constants.selection_index, light)) return 0.0f.xxxx;
    if (!SpotEmitterHasSpatialSupport(light, world_position)) return 0.0f.xxxx;
    const LightShadowReference shadow_reference = LoadLightShadowReference(
        lighting_bindings.local_shadow_map_srv, light.selection_index);
    const float3 light_vector
        = light.position_ws - world_position;
    const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(
        screen_uv, world_position, camera_position, bindings);
    if (dot(surface.world_normal, light_vector) + light.source_radius_m <= 0.0)
        return 0.0f.xxxx;
    const float shadow_visibility = ComputeLocalShadowVisibility(shadow_reference,
        world_position, surface.world_normal, VortexSafeNormalize(light_vector));
    if (shadow_visibility <= 0.0) return 0.0f.xxxx;
    const float3 lighting = EvaluateLocalEmitterResponse(light, world_position,
        surface.world_normal, surface.view_direction, surface.specular_f0,
        surface.base_color * (1.0 - surface.metallic), surface.roughness,
        lighting_bindings) * shadow_visibility;
    RecordHdrSceneSource(lighting, 2u);
    return float4(lighting * GetPreExposure(), 0.0f);
}

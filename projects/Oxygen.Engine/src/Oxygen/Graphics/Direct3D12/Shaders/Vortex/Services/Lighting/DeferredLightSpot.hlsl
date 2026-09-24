//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"
#include "Vortex/Services/Lighting/FiniteEmitter.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Vortex/Services/Shadows/ContactShadow.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
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
    // The pass selects this source family; specialize the shared emitter code.
    light.kind = FORWARD_LOCAL_LIGHT_SPOT;
    LocalEmitterInput source;
    if (!PrepareLocalEmitterInput(light, world_position, source)) return 0.0f.xxxx;
    const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(
        screen_uv, world_position, camera_position, bindings);
    if (!LocalEmitterFacesSurface(light, source, surface.world_normal)) return 0.0f.xxxx;
    const LightShadowReference shadow_reference = LoadLightShadowReference(
        lighting_bindings.local_shadow_map_srv, light.selection_index);
    const float shadow_visibility = surface.receives_shadows
        ? ComputeLocalShadowVisibility(shadow_reference,
            world_position, surface.world_normal, source.direction_to_center)
            * ComputeContactShadowVisibility(light.flags, true, world_position,
                surface.geometric_normal, source.direction_to_center) : 1.0f;
    if (shadow_visibility <= 0.0) return 0.0f.xxxx;
    const GgxDirectContext brdf = PrepareGgxDirect(surface.world_normal,
        surface.view_direction, surface.specular_f0,
        surface.base_color * (1.0 - surface.metallic), surface.roughness, lighting_bindings);
    const GgxDirectLobes lobes = EvaluatePreparedLocalEmitterLobes(light, source,
        surface.world_normal, surface.view_direction, brdf);
    const float3 lighting = (lobes.single_scattering + lobes.multiple_scattering + lobes.diffuse)
        * shadow_visibility;
    RecordHdrSceneSource(lighting, 2u);
    return float4(lighting * GetPreExposure(), 0.0f);
}

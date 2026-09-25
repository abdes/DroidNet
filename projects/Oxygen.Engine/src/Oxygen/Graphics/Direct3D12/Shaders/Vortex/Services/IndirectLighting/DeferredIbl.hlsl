//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Services/Lighting/DeferredShadingCommon.hlsli"
#include "Vortex/Services/IndirectLighting/IblEvaluation.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Contracts/View/HdrConsumerInputs.hlsli"
[shader("vertex")]
VortexFullscreenTriangleOutput DeferredIblVS(uint id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(id);
}
[shader("pixel")]
float4 DeferredIblPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const SceneTextureBindingData textures = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    if (!HasDeferredLightingInputs(textures)) return 0.0.xxxx;
    const float depth = SampleSceneDepth(input.uv, textures);
    if (IsDeferredBackgroundDepth(depth)) return 0.0.xxxx;
    const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(input.uv,
        ReconstructDeferredWorldPosition(input.uv, depth), camera_position, textures);
    if (surface.shading_model != SHADING_MODEL_DEFAULT_LIT) return 0.0.xxxx;
    EnvironmentStaticData environment;
    if (!LoadEnvironmentStaticData(environment)) return 0.0.xxxx;
    const IblSurfaceLighting lighting = EvaluateSkyIbl(environment.sky_light, ResolveIblBrdfSlot(),
        surface.world_normal, surface.view_direction, surface.base_color, surface.metallic,
        surface.perceptual_roughness, surface.ambient_occlusion, surface.specular_f0);
    const float3 radiance = lighting.diffuse + lighting.specular;
    RecordHdrSceneSource(radiance, 3u);
    return float4(radiance * GetPreExposure(), 0.0);
}

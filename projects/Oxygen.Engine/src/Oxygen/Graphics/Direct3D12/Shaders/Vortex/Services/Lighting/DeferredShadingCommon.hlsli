//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDSHADINGCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDSHADINGCOMMON_HLSLI

#include "Vortex/Shared/Math.hlsli"

#include "Vortex/Contracts/Scene/GBufferHelpers.hlsli"
#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static const float kVortexDeferredMinRoughness = 0.045f;

struct DeferredLightingSurfaceData
{
    float3 world_position;
    float3 world_normal;
    float3 geometric_normal;
    float3 base_color;
    float3 view_direction;
    float3 specular_f0;
    float metallic;
    float specular;
    float roughness;
    float perceptual_roughness;
    float ambient_occlusion;
    uint shading_model;
    bool receives_shadows;
};

static inline bool HasDeferredLightingInputs(SceneTextureBindingData bindings)
{
    return IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_SCENE_DEPTH)
        && IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_GBUFFERS);
}

static bool IsDeferredBackgroundDepth(float depth)
{
    return reverse_z != 0u ? depth <= 0.0 : depth >= 1.0;
}

static inline float3 ReconstructDeferredWorldPosition(
    float2 screen_uv, float device_depth)
{
    return ReconstructWorldPosition(
        screen_uv, device_depth, inverse_view_projection_matrix);
}

static inline DeferredLightingSurfaceData LoadDeferredLightingSurface(
    float2 uv,
    float3 world_position,
    float3 camera_position_ws,
    SceneTextureBindingData bindings)
{
    DeferredLightingSurfaceData surface = (DeferredLightingSurfaceData)0;
    surface.world_position = world_position;

    const GBufferData gbuffer = ReadGBuffer(uv, bindings);
    surface.world_normal = VortexSafeNormalize(gbuffer.world_normal);
    surface.receives_shadows = gbuffer.receives_shadows;
    surface.geometric_normal = gbuffer.geometric_normal;
    surface.base_color = max(gbuffer.base_color, 0.0f.xxx);
    surface.view_direction
        = ResolveSurfaceViewDirection(surface.world_position, camera_position_ws,
            inverse_view_projection_matrix, is_orthographic, reverse_z);
    surface.metallic = saturate(gbuffer.metallic);
    surface.specular = saturate(gbuffer.specular);
    surface.perceptual_roughness = saturate(gbuffer.roughness);
    surface.roughness = max(surface.perceptual_roughness, kVortexDeferredMinRoughness);
    surface.ambient_occlusion = saturate(gbuffer.ambient_occlusion);
    surface.shading_model = gbuffer.shading_model;
    surface.specular_f0 = ComputeMetallicF0(
        surface.base_color, surface.metallic, surface.specular);
    return surface;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDSHADINGCOMMON_HLSLI

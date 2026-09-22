//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDLIGHTINGCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDLIGHTINGCOMMON_HLSLI

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Services/Lighting/LocalLightAttenuation.hlsli"

#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Contracts/View/HdrConsumerInputs.hlsli"
#include "Vortex/Services/Lighting/DeferredShadingCommon.hlsli"

#include "Vortex/Contracts/Lighting/DeferredLightConstants.hlsli"

struct DeferredLightVolumeVSOutput
{
    float4 screen_position : TEXCOORD0;
    float4 position : SV_POSITION;
};

static inline float3 LoadDeferredLightGeometryVertex(
    uint light_geometry_vertices_srv,
    uint vertex_id)
{
    if (light_geometry_vertices_srv == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxx;
    }

    StructuredBuffer<float4> light_geometry_vertices =
        ResourceDescriptorHeap[light_geometry_vertices_srv];
    return light_geometry_vertices[vertex_id].xyz;
}

static inline SceneTextureBindingData LoadBindingsFromCurrentView()
{
    return LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
}

static inline DeferredLightVolumeVSOutput GenerateDeferredLightVolume(
    float3 local_position, float4x4 light_world_matrix)
{
    DeferredLightVolumeVSOutput output = (DeferredLightVolumeVSOutput)0;
    const float4 world_position = mul(light_world_matrix, float4(local_position, 1.0f));
    const float4 view_position = mul(view_matrix, world_position);
    output.screen_position = mul(projection_matrix, view_position);
    output.position = output.screen_position;
    return output;
}

static inline float2 ResolveDeferredLightScreenUv(float4 screen_position)
{
    const float safe_w = max(abs(screen_position.w), 1.0e-6f);
    if (safe_w <= 0.0f) {
        return 0.0f.xx;
    }

    float2 uv = screen_position.xy / safe_w * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;
    return uv;
}

#endif

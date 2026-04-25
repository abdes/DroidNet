//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Scene/GBufferHelpers.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

static float3 EvaluateBasePassDebugView(
    float2 uv, SceneTextureBindingData bindings)
{
#if defined(DEBUG_BASE_COLOR)
    const GBufferData data = ReadGBuffer(uv, bindings);
    return saturate(data.base_color);
#elif defined(DEBUG_WORLD_NORMALS)
    const GBufferData data = ReadGBuffer(uv, bindings);
    return saturate(data.world_normal * 0.5f + 0.5f);
#elif defined(DEBUG_ROUGHNESS)
    const GBufferData data = ReadGBuffer(uv, bindings);
    return saturate(data.roughness.xxx);
#elif defined(DEBUG_METALNESS)
    const GBufferData data = ReadGBuffer(uv, bindings);
    return saturate(data.metallic.xxx);
#elif defined(DEBUG_SCENE_DEPTH_RAW)
    if (!IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_SCENE_DEPTH))
    {
        return float3(1.0f, 0.0f, 1.0f);
    }
    const float device_depth = SampleSceneDepth(uv, bindings);
    return saturate(device_depth).xxx;
#elif defined(DEBUG_SCENE_DEPTH_LINEAR)
    if (!IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_SCENE_DEPTH))
    {
        return float3(1.0f, 0.0f, 1.0f);
    }
    const float device_depth = SampleSceneDepth(uv, bindings);
    const float3 world_position = ReconstructWorldPosition(
        uv, device_depth, inverse_view_projection_matrix);
    const float4 view_position = mul(view_matrix, float4(world_position, 1.0f));
    const float linear_eye_depth = max(-view_position.z, 0.0f);
    const float display = saturate(1.0f / (1.0f + linear_eye_depth));
    return display.xxx;
#else
    return float3(1.0f, 0.0f, 1.0f);
#endif
}

[shader("vertex")]
VortexFullscreenTriangleOutput BasePassDebugViewVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 BasePassDebugViewPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const SceneTextureBindingData bindings
        = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    if (!IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_GBUFFERS)) {
#if !defined(DEBUG_SCENE_DEPTH_RAW) && !defined(DEBUG_SCENE_DEPTH_LINEAR)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
#endif
    }

    return float4(EvaluateBasePassDebugView(input.uv, bindings), 1.0f);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/GBufferHelpers.hlsli"
#include "Vortex/Contracts/ViewFrameBindings.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Renderer/ViewConstants.hlsli"

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
    const float projection_zz = projection_matrix._33;
    const float projection_zw = projection_matrix._34;
    const float projection_wz = projection_matrix._43;
    const float epsilon = 1.0e-6f;
    float linear_eye_depth = 0.0f;
    if (abs(projection_zw) > 0.5f)
    {
        linear_eye_depth = max(
            projection_wz / max(device_depth + projection_zz, epsilon), 0.0f);
    }
    else if (abs(projection_zz) > epsilon)
    {
        linear_eye_depth
            = max((projection_wz - device_depth) / projection_zz, 0.0f);
    }
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

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/ViewConstants.hlsli"

#include "Vortex/Contracts/SceneTextures.hlsli"
#include "Vortex/Contracts/ViewFrameBindings.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static const float kStandardSkyLuminance = 5000.0f;

static inline bool IsReverseZProjection()
{
    return projection_matrix._33 > 0.0f;
}

static inline float ResolveFarDepthReference()
{
    return IsReverseZProjection() ? 0.0f : 1.0f;
}

static inline float EvaluateFarBackgroundMask(float scene_depth)
{
    const float far_depth = ResolveFarDepthReference();
    const float epsilon = 1.0e-3f;
    return saturate(1.0f - abs(scene_depth - far_depth) / epsilon);
}

static inline float3 EvaluateFogColor(float distance_factor, float altitude_factor)
{
    const float3 near_color = float3(0.34f, 0.38f, 0.43f);
    const float3 far_color = float3(0.63f, 0.67f, 0.72f);
    return lerp(near_color, far_color, saturate(distance_factor + altitude_factor * 0.35f));
}

[shader("vertex")]
VortexFullscreenTriangleOutput VortexFogPassVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexFogPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const SceneTextureBindingData bindings
        = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    const float raw_depth = SampleSceneDepth(input.uv, bindings);
    const float reconstruct_depth
        = EvaluateFarBackgroundMask(raw_depth) > 0.0f ? ResolveFarDepthReference() : raw_depth;
    const float3 world_position = ReconstructWorldPosition(
        input.uv, reconstruct_depth, inverse_view_projection_matrix);
    const float3 view_vector = world_position - camera_position;
    const float distance_to_sample = length(view_vector);
    const float far_background = EvaluateFarBackgroundMask(raw_depth);

    const float distance_factor = 1.0f - exp(-distance_to_sample * 0.012f);
    const float height_factor = exp(-max(world_position.y + 4.0f, 0.0f) * 0.08f);
    const float fog_alpha = saturate(
        distance_factor * height_factor * 0.65f + far_background * 0.12f);
    const float3 fog_color = EvaluateFogColor(distance_factor, height_factor);
    return float4(fog_color * fog_alpha * kStandardSkyLuminance, fog_alpha);
}

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

static inline float3 EvaluateAtmosphereColor(float3 view_direction)
{
    const float upward = saturate(view_direction.y * 0.5f + 0.5f);
    const float horizon = saturate(1.0f - abs(view_direction.y));
    const float3 low_altitude = float3(0.22f, 0.33f, 0.52f);
    const float3 high_altitude = float3(0.56f, 0.69f, 0.92f);
    return lerp(low_altitude, high_altitude, upward) + horizon * 0.08f.xxx;
}

[shader("vertex")]
VortexFullscreenTriangleOutput VortexAtmosphereComposeVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexAtmosphereComposePS(VortexFullscreenTriangleOutput input) : SV_Target0
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
    const float3 view_direction = distance_to_sample > 1.0e-4f
        ? view_vector / distance_to_sample
        : normalize(float3(input.uv - 0.5f, 1.0f));

    const float far_background = EvaluateFarBackgroundMask(raw_depth);
    const float distance_density = 1.0f - exp(-distance_to_sample * 0.0015f);
    const float altitude_density = exp(-max(world_position.y - camera_position.y, 0.0f) * 0.02f);
    const float atmosphere_alpha = saturate(
        distance_density * altitude_density * 0.55f + far_background * 0.18f);
    const float3 atmosphere_color = EvaluateAtmosphereColor(view_direction);
    return float4(
        atmosphere_color * atmosphere_alpha * kStandardSkyLuminance,
        atmosphere_alpha);
}

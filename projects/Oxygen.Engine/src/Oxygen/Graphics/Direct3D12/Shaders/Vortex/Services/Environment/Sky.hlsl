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

static inline bool IsReverseZProjection()
{
    return projection_matrix._33 < 0.0f;
}

static inline float ResolveFarDepthReference()
{
    return IsReverseZProjection() ? 0.0f : 1.0f;
}

static inline bool EvaluateFarBackgroundMask(float scene_depth)
{
    const float far_depth = ResolveFarDepthReference();
    const float epsilon = 1.0e-3f;
    return abs(scene_depth - far_depth) <= epsilon;
}

static inline float3 ReconstructViewDirection(float2 uv)
{
    const float3 far_world_position = ReconstructWorldPosition(
        uv, ResolveFarDepthReference(), inverse_view_projection_matrix);
    return normalize(far_world_position - camera_position);
}

static inline float3 EvaluateSkyColor(float3 view_direction)
{
    const float upward = saturate(view_direction.y * 0.5f + 0.5f);
    const float horizon = exp2(-abs(view_direction.y) * 7.0f);
    const float sun_amount = saturate(dot(
        view_direction, normalize(float3(0.25f, 0.9f, 0.35f))));
    const float sun_disc = pow(sun_amount, 96.0f);
    const float3 zenith = float3(0.11f, 0.22f, 0.47f);
    const float3 horizon_color = float3(0.72f, 0.48f, 0.24f);
    const float3 night_color = float3(0.03f, 0.05f, 0.09f);
    float3 sky = lerp(night_color, zenith, upward);
    sky += horizon * horizon_color * 0.35f;
    sky += sun_disc * float3(1.0f, 0.85f, 0.55f) * 2.5f;
    return saturate(sky);
}

[shader("vertex")]
VortexFullscreenTriangleOutput VortexSkyPassVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexSkyPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const SceneTextureBindingData bindings
        = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    const float scene_depth = SampleSceneDepth(input.uv, bindings);
    if (!EvaluateFarBackgroundMask(scene_depth)) {
        discard;
    }

    const float3 view_direction = ReconstructViewDirection(input.uv);
    const float3 sky_color = EvaluateSkyColor(view_direction);
    return float4(sky_color, 1.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexIblIrradianceCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    (void)dispatch_id;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexIblPrefilterCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    (void)dispatch_id;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Environment/EnvironmentFrameBindings.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"

#include "Vortex/Services/Environment/AerialPerspective.hlsli"
#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static inline bool IsReverseZProjection()
{
    return reverse_z != 0u;
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
    const float far_background = EvaluateFarBackgroundMask(raw_depth);
    // Far-background (sky-dome) pixels are produced by the dedicated sky pass
    // using the full SkyView LUT. The camera aerial-perspective volume only
    // covers a finite near-camera range (tens of km); sampling its far slice
    // for sky rays would overwrite the real sky color with near-zero aerial
    // inscatter. Match UE5 by restricting AP composition to in-atmosphere
    // geometry.
    if (far_background > 0.0f) {
        discard;
    }
    const float reconstruct_depth = raw_depth;
    const float3 world_position = ReconstructWorldPosition(
        input.uv,
        reconstruct_depth,
        inverse_view_projection_matrix);

    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data))
    {
        discard;
    }

    const float3 sun_dir = GetSunDirectionWS();
    const AerialPerspectiveResult aerial = ComputeAerialPerspective(
        env_data,
        world_position,
        camera_position,
        sun_dir);

    const float3 inscatter = aerial.inscatter;
    const float transmittance = saturate(dot(aerial.transmittance, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f)));

    const float atmosphere_alpha = saturate(1.0f - transmittance);
    const float3 atmosphere_color = atmosphere_alpha > 1.0e-5f
        ? inscatter / atmosphere_alpha
        : 0.0f.xxx;
    return float4(atmosphere_color, atmosphere_alpha);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/View/HdrConsumerInputs.hlsli"
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"
#include "Vortex/Services/Environment/HeightFog.hlsli"
#include "Vortex/Services/Environment/LocalFogVolumePixel.hlsli"

// g_PassConstantsIndex names the local-fog compose constants when local fog
// is injected into volumetric fog; this pass then traces local fog beyond the
// volume, as UE's height fog pass does.
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

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
VortexFullscreenTriangleOutput VortexFogPassVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexFogPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data)) {
        return 0.0f.xxxx;
    }

    const GpuFogParams fog = env_data.fog;
    if (!FogFlagEnabled(fog.flags, GPU_FOG_FLAG_ENABLED)) {
        return 0.0f.xxxx;
    }

    EnvironmentViewData environment_view;
    if (!TryLoadResolvedEnvironmentViewData(environment_view)) {
        discard;
    }
    if (!FogFlagEnabled(fog.flags, GPU_FOG_FLAG_RENDER_IN_MAIN_PASS)) {
        return 0.0f.xxxx;
    }
    const bool reflection_capture =
        (environment_view.flags & (1u << 1u)) != 0u;
    if (reflection_capture
        && !FogFlagEnabled(fog.flags, GPU_FOG_FLAG_VISIBLE_IN_REFLECTION_CAPTURES)) {
        return 0.0f.xxxx;
    }

    const SceneTextureBindingData bindings =
        LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    float raw_depth;
    if (!TrySampleSceneDepth(input.uv, bindings, raw_depth)) {
        discard;
    }
    // The sky pass fogs far-depth pixels, volumetric fog included.
    if (EvaluateFarBackgroundMask(raw_depth) > 0.999f) {
        return 0.0f.xxxx;
    }

    const float3 world_position = ReconstructWorldPosition(
        input.uv,
        raw_depth,
        inverse_view_projection_matrix);
    const float3 camera_to_receiver = world_position - camera_position;
    const float view_depth_m =
        max(-mul(view_matrix, float4(world_position, 1.0f)).z, 0.0f);
    const bool volumetric_usable =
        IsIntegratedVolumetricFogUsable(env_data.volumetric_fog);
    // Volumetric fog covers view depths up to its distance; exclude analytic
    // height fog from that range along this ray, as UE does, so the two media
    // are not counted twice.
    GpuFogParams height_fog_params = fog;
    if (volumetric_usable && view_depth_m > 0.0f) {
        const float ray_per_view_depth = length(camera_to_receiver) / view_depth_m;
        height_fog_params.start_distance_m = max(fog.start_distance_m,
            env_data.volumetric_fog.distance_m * ray_per_view_depth);
    }
    float4 height_fog = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (FogFlagEnabled(fog.flags, GPU_FOG_FLAG_HEIGHT_FOG_ENABLED)
        && (fog.primary_density > 0.0f || fog.secondary_density > 0.0f)) {
        height_fog = EvaluateHeightFogSegment(
            height_fog_params,
            env_data,
            environment_view,
            camera_position,
            camera_to_receiver);
    }
    // Local fog lies in front of the height fog and behind the volume.
    float4 analytic_fog = height_fog;
    if (g_PassConstantsIndex != K_INVALID_BINDLESS_INDEX && volumetric_usable) {
        StructuredBuffer<LocalFogComposePassConstants> local_fog_constants =
            ResourceDescriptorHeap[g_PassConstantsIndex];
        const float4 local_fog = EvaluateLocalFogVolumesAtPixel(
            local_fog_constants[0], input.uv, raw_depth, false,
            env_data.volumetric_fog.distance_m);
        analytic_fog = float4(local_fog.rgb + height_fog.rgb * local_fog.a,
            local_fog.a * height_fog.a);
    }
    // The certificate bounds the volume's attenuation of this analytic fog.
    RecordHdrConsumerInput(analytic_fog.rgb, HDR_INPUT_HEIGHT_FOG);
    analytic_fog.rgb *= GetPreExposure();
    const float4 volumetric_fog = SampleIntegratedVolumetricFog(
        env_data.volumetric_fog,
        input.uv,
        view_depth_m);
    const float4 fog_result = ComposeFogResults(analytic_fog, volumetric_fog);
    const bool holdout = !reflection_capture && FogFlagEnabled(fog.flags, GPU_FOG_FLAG_HOLDOUT);
    return float4(holdout ? 0.0f.xxx : fog_result.rgb, 1.0f - fog_result.a);
}

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

static inline bool VolumetricFogFlagEnabled(uint flags, uint bit)
{
    return (flags & bit) != 0u;
}

// The froxel grid slices view-space depth along the camera axis, so the
// lookup must use the receiver's view depth, not its radial distance: off-axis
// the radial distance lands behind the surface, in cells that can be opaque
// (exponential height fog grows without bound below its height offset).
static bool IsIntegratedVolumetricFogUsable(GpuVolumetricFogParams volumetric_fog)
{
    return VolumetricFogFlagEnabled(volumetric_fog.flags, GPU_VOLUMETRIC_FOG_FLAG_ENABLED)
        && VolumetricFogFlagEnabled(
            volumetric_fog.flags,
            GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID)
        && volumetric_fog.integrated_light_scattering_srv != K_INVALID_BINDLESS_INDEX
        && BX_IN_TEXTURES(volumetric_fog.integrated_light_scattering_srv);
}

static float4 SampleIntegratedVolumetricFog(
    GpuVolumetricFogParams volumetric_fog,
    float2 uv,
    float view_depth_m)
{
    if (!IsIntegratedVolumetricFogUsable(volumetric_fog)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float depth_span =
        max(volumetric_fog.distance_m - volumetric_fog.start_distance_m, 1.0f);
    const float linear_depth_fraction =
        saturate((view_depth_m - volumetric_fog.start_distance_m) / depth_span);
    float depth_fraction = sqrt(linear_depth_fraction);
    if (abs(volumetric_fog.grid_z_params.x) > 1.0e-8f
        && abs(volumetric_fog.grid_z_params.z) > 1.0e-4f
        && volumetric_fog.grid_depth > 0u) {
        const float z_argument = max(
            view_depth_m * volumetric_fog.grid_z_params.x
            + volumetric_fog.grid_z_params.y,
            1.0e-8f);
        const float z_slice =
            log2(z_argument) * volumetric_fog.grid_z_params.z;
        depth_fraction = saturate(
            z_slice / max(float(volumetric_fog.grid_depth), 1.0f));
    }
    Texture3D<float4> integrated_light_scattering =
        ResourceDescriptorHeap[volumetric_fog.integrated_light_scattering_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
    return integrated_light_scattering.SampleLevel(
        linear_sampler,
        float3(uv, depth_fraction),
        0.0f);
}

static float4 ComposeFogResults(float4 height_fog, float4 volumetric_fog)
{
    return float4(
        volumetric_fog.rgb + height_fog.rgb * volumetric_fog.a,
        saturate(height_fog.a * volumetric_fog.a));
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

    const EnvironmentViewData environment_view = LoadResolvedEnvironmentViewData();
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
    const float raw_depth = SampleSceneDepth(input.uv, bindings);
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
    RecordHdrConsumerInput(height_fog.rgb, HDR_INPUT_HEIGHT_FOG);
    height_fog.rgb *= GetPreExposure();
    const float4 volumetric_fog = SampleIntegratedVolumetricFog(
        env_data.volumetric_fog,
        input.uv,
        view_depth_m);
    const float4 fog_result = ComposeFogResults(height_fog, volumetric_fog);
    const bool holdout = !reflection_capture && FogFlagEnabled(fog.flags, GPU_FOG_FLAG_HOLDOUT);
    return float4(holdout ? 0.0f.xxx : fog_result.rgb, 1.0f - fog_result.a);
}

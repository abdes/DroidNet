//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_VOLUMETRIC_FOG_SAMPLING_HLSLI
#define OXYGEN_VORTEX_VOLUMETRIC_FOG_SAMPLING_HLSLI

#include "Vortex/Contracts/Environment/EnvironmentStaticData.hlsli"

static inline bool VolumetricFogFlagEnabled(uint flags, uint bit)
{
    return (flags & bit) != 0u;
}

static bool IsIntegratedVolumetricFogUsable(GpuVolumetricFogParams volumetric_fog)
{
    return VolumetricFogFlagEnabled(volumetric_fog.flags, GPU_VOLUMETRIC_FOG_FLAG_ENABLED)
        && VolumetricFogFlagEnabled(
            volumetric_fog.flags,
            GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID)
        && volumetric_fog.integrated_light_scattering_srv != K_INVALID_BINDLESS_INDEX
        && BX_IN_TEXTURES(volumetric_fog.integrated_light_scattering_srv);
}

// Samples the integrated (pre-exposed) light scattering and transmittance at a
// normalized depth through the froxel grid; 1 is the end of the volume.
static float4 SampleIntegratedVolumetricFogAtDepthFraction(
    GpuVolumetricFogParams volumetric_fog,
    float2 uv,
    float depth_fraction)
{
    if (!IsIntegratedVolumetricFogUsable(volumetric_fog)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    Texture3D<float4> integrated_light_scattering =
        ResourceDescriptorHeap[volumetric_fog.integrated_light_scattering_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[K_SAMPLER_LINEAR_CLAMP];
    return integrated_light_scattering.SampleLevel(
        linear_sampler,
        float3(uv, saturate(depth_fraction)),
        0.0f);
}

// The froxel grid slices view-space depth along the camera axis, so the
// lookup must use the receiver's view depth, not its radial distance: off-axis
// the radial distance lands behind the surface, in cells that can be opaque
// (exponential height fog grows without bound below its height offset).
static float4 SampleIntegratedVolumetricFog(
    GpuVolumetricFogParams volumetric_fog,
    float2 uv,
    float view_depth_m)
{
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
    return SampleIntegratedVolumetricFogAtDepthFraction(
        volumetric_fog, uv, depth_fraction);
}

// Volumetric fog lies in front of the analytic fog behind it, as UE's
// CombineVolumetricFog composes them.
static float4 ComposeFogResults(float4 height_fog, float4 volumetric_fog)
{
    return float4(
        volumetric_fog.rgb + height_fog.rgb * volumetric_fog.a,
        saturate(height_fog.a * volumetric_fog.a));
}

// Ray length per unit of view depth along a world-space direction, or 0 when
// the direction does not advance along the view axis.
static float RayLengthPerViewDepth(float4x4 view_matrix_ws, float3 direction)
{
    const float view_depth_per_unit = -mul(view_matrix_ws, float4(direction, 0.0f)).z;
    return view_depth_per_unit > 1.0e-6f ? rcp(view_depth_per_unit) : 0.0f;
}

#endif

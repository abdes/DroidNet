//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Passes/Lighting/ClusterLookup.hlsli"

/**
 * Loads the EnvironmentStaticData from the global ResourceDescriptorHeap using the provided slot.
 *
 * This is the canonical path for all environment-aware shaders to fetch static environment parameters
 * like sky, atmosphere, fog, and post-process settings.
 *
 * @param bindless_slot The SRV index from SceneConstants.bindless_env_static_slot.
 * @param frame_slot The current frame slot index from SceneConstants.frame_slot.
 * @param[out] out_data The loaded environment data.
 * @return True if the slot is valid and data was loaded; false otherwise.
 */
static inline bool LoadEnvironmentStaticData(uint bindless_slot, uint frame_slot, out EnvironmentStaticData out_data)
{
    if (bindless_slot != K_INVALID_BINDLESS_INDEX && BX_IN_GLOBAL_SRV(bindless_slot))
    {
        // Access via global ResourceDescriptorHeap (pure bindless)
        StructuredBuffer<EnvironmentStaticData> env_buffer = ResourceDescriptorHeap[bindless_slot];
        out_data = env_buffer[frame_slot];
        return true;
    }

    out_data = (EnvironmentStaticData)0;
    return false;
}

/**
 * Helper to fetch the current exposure from the dynamic environment data.
 *
 * @return The resolved exposure value for the current view.
 */
static inline float GetExposure()
{
    return EnvironmentDynamicData.exposure;
}

/**
 * Computes the cluster index for the current pixel using the global environment dynamic data.
 *
 * @param screen_pos Pixel coordinates (SV_Position.xy).
 * @param linear_depth View-space linear depth.
 * @return The linear cluster index into the cluster grid.
 */
static inline uint GetClusterIndex(float2 screen_pos, float linear_depth)
{
    const uint3 cluster_dims = uint3(EnvironmentDynamicData.cluster_dim_x,
                                     EnvironmentDynamicData.cluster_dim_y,
                                     EnvironmentDynamicData.cluster_dim_z);
    const float2 screen_dims = float2(EnvironmentDynamicData.cluster_dim_x * EnvironmentDynamicData.tile_size_px,
                                      EnvironmentDynamicData.cluster_dim_y * EnvironmentDynamicData.tile_size_px);

    return ComputeClusterIndex(
        screen_pos,
        linear_depth,
        screen_dims,
        cluster_dims,
        EnvironmentDynamicData.tile_size_px,
        EnvironmentDynamicData.z_near,
        EnvironmentDynamicData.z_scale,
        EnvironmentDynamicData.z_bias);
}

/**
 * Returns the cluster grid dimensions from the dynamic environment data.
 */
static inline uint3 GetClusterDimensions()
{
    return uint3(EnvironmentDynamicData.cluster_dim_x,
                 EnvironmentDynamicData.cluster_dim_y,
                 EnvironmentDynamicData.cluster_dim_z);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI

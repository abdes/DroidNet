//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGHELPERS_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/LightingFrameBindings.hlsli"
#include "Renderer/ViewConstants.hlsli"
#include "Renderer/ViewFrameBindings.hlsli"
#include "Lighting/ClusterLookup.hlsli"

static inline LightingFrameBindings LoadResolvedLightingFrameBindings()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    return LoadLightingFrameBindings(view_bindings.lighting_frame_slot);
}

static inline float3 GetSunDirectionWS()
{
    return LoadResolvedLightingFrameBindings().sun.direction_ws_illuminance.xyz;
}

static inline float GetSunIlluminance()
{
    return LoadResolvedLightingFrameBindings().sun.direction_ws_illuminance.w;
}

static inline float3 GetSunColorRGB()
{
    return LoadResolvedLightingFrameBindings().sun.color_rgb_intensity.xyz;
}

static inline float GetSunIntensity()
{
    return LoadResolvedLightingFrameBindings().sun.color_rgb_intensity.w;
}

static inline float3 GetSunLuminanceRGB()
{
    return GetSunColorRGB() * GetSunIlluminance();
}

static inline bool HasSunLight()
{
    return LoadResolvedLightingFrameBindings().sun.enabled != 0u;
}

static inline uint3 GetClusterDimensions()
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    return uint3(lighting.light_culling.cluster_dim_x,
                 lighting.light_culling.cluster_dim_y,
                 lighting.light_culling.cluster_dim_z);
}

static inline uint GetClusterGridSlot()
{
    return LoadResolvedLightingFrameBindings().light_culling.bindless_cluster_grid_slot;
}

static inline uint GetClusterIndexListSlot()
{
    return LoadResolvedLightingFrameBindings().light_culling.bindless_cluster_index_list_slot;
}

static inline uint GetClusterMaxLightsPerCell()
{
    return LoadResolvedLightingFrameBindings().light_culling.max_lights_per_cell;
}

static inline uint GetClusterIndex(float2 screen_pos, float linear_depth)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    const uint3 cluster_dims = uint3(lighting.light_culling.cluster_dim_x,
                                     lighting.light_culling.cluster_dim_y,
                                     lighting.light_culling.cluster_dim_z);

    return ComputeClusterIndex(
        screen_pos,
        linear_depth,
        cluster_dims,
        lighting.light_culling.light_grid_pixel_size_shift,
        float3(
            lighting.light_culling.light_grid_z_params_b,
            lighting.light_culling.light_grid_z_params_o,
            lighting.light_culling.light_grid_z_params_s));
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGHELPERS_HLSLI

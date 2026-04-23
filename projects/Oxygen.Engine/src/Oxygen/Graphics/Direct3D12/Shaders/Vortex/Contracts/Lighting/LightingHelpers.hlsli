//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGHELPERS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Lighting/LightingFrameBindings.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Services/Lighting/ClusterLookup.hlsli"

static inline LightingFrameBindings LoadResolvedLightingFrameBindings()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    return LoadLightingFrameBindings(view_bindings.lighting_frame_slot);
}

static inline float3 GetSunDirectionWS()
{
    return LoadResolvedLightingFrameBindings().directional.direction;
}

static inline float GetSunIlluminance()
{
    return LoadResolvedLightingFrameBindings().directional.illuminance_lux;
}

static inline float3 GetSunColorRGB()
{
    return LoadResolvedLightingFrameBindings().directional.color;
}

static inline float GetSunIntensity()
{
    return LoadResolvedLightingFrameBindings().directional.illuminance_lux;
}

static inline float3 GetSunLuminanceRGB()
{
    return GetSunColorRGB() * GetSunIlluminance();
}

static inline bool HasSunLight()
{
    return LoadResolvedLightingFrameBindings().has_directional_light != 0u;
}

static inline uint3 GetClusterDimensions()
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    return uint3(lighting.grid_size);
}

static inline uint GetClusterGridSlot()
{
    return LoadResolvedLightingFrameBindings().grid_indirection_srv;
}

static inline uint GetClusterIndexListSlot()
{
    return LoadResolvedLightingFrameBindings().light_view_data_srv;
}

static inline uint GetClusterMaxLightsPerCell()
{
    return LoadResolvedLightingFrameBindings().max_culled_lights_per_cell;
}

static inline uint GetClusterIndex(float2 screen_pos, float linear_depth)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    const uint3 cluster_dims = uint3(lighting.grid_size);

    return ComputeClusterIndex(
        screen_pos,
        linear_depth,
        cluster_dims,
        6u,
        lighting.grid_z_params);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGHELPERS_HLSLI

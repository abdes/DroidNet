//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTUREBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTUREBINDINGS_HLSLI

#include "Definitions/SceneDefinitions.hlsli"
#include "GBufferLayout.hlsli"

struct SceneTextureBindingData
{
    uint scene_color_srv;
    uint scene_depth_srv;
    uint partial_depth_srv;
    uint velocity_srv;
    uint stencil_srv;
    uint custom_depth_srv;
    uint custom_stencil_srv;
    uint gbuffer_srvs[GBUFFER_BINDING_COUNT];
    uint scene_color_uav;
    uint velocity_uav;
    uint valid_flags;
};

static inline SceneTextureBindingData MakeInvalidSceneTextureBindings()
{
    // Invalid bindless index sentinel mirrors the CPU contract: 0xFFFFFFFFu / UINT_MAX.
    SceneTextureBindingData bindings;
    bindings.scene_color_srv = INVALID_BINDLESS_INDEX;
    bindings.scene_depth_srv = INVALID_BINDLESS_INDEX;
    bindings.partial_depth_srv = INVALID_BINDLESS_INDEX;
    bindings.velocity_srv = INVALID_BINDLESS_INDEX;
    bindings.stencil_srv = INVALID_BINDLESS_INDEX;
    bindings.custom_depth_srv = INVALID_BINDLESS_INDEX;
    bindings.custom_stencil_srv = INVALID_BINDLESS_INDEX;
    bindings.gbuffer_srvs[GBUFFER_NORMAL] = INVALID_BINDLESS_INDEX;
    bindings.gbuffer_srvs[GBUFFER_MATERIAL] = INVALID_BINDLESS_INDEX;
    bindings.gbuffer_srvs[GBUFFER_BASE_COLOR] = INVALID_BINDLESS_INDEX;
    bindings.gbuffer_srvs[GBUFFER_CUSTOM_DATA] = INVALID_BINDLESS_INDEX;
    bindings.gbuffer_srvs[GBUFFER_SHADOW_FACTORS] = INVALID_BINDLESS_INDEX;
    bindings.gbuffer_srvs[GBUFFER_WORLD_TANGENT] = INVALID_BINDLESS_INDEX;
    bindings.scene_color_uav = INVALID_BINDLESS_INDEX;
    bindings.velocity_uav = INVALID_BINDLESS_INDEX;
    bindings.valid_flags = 0u;
    return bindings;
}

static inline bool IsSceneTextureValid(SceneTextureBindingData bindings, uint flag)
{
    return (bindings.valid_flags & flag) != 0u;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTUREBINDINGS_HLSLI

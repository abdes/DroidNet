//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Environment/EnvironmentFrameBindings.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentStaticData.hlsli"
#include "Vortex/Contracts/Environment/SkyDiffuseIrradiance.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

/**
 * Loads the EnvironmentStaticData for the current view.
 *
 * This is the canonical path for all environment-aware shaders to fetch static environment parameters
 * like sky, atmosphere, fog, and post-process settings.
 *
 * @param[out] out_data The loaded environment data.
 * @return True if the current view resolved an environment static slot and data was loaded; false otherwise.
 */
static inline bool LoadEnvironmentStaticData(out EnvironmentStaticData out_data)
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    if (view_bindings.environment_frame_slot == K_INVALID_BINDLESS_INDEX) {
        out_data = (EnvironmentStaticData)0;
        return false;
    }

    const EnvironmentFrameBindings environment_bindings =
        LoadEnvironmentFrameBindings(view_bindings.environment_frame_slot);
    const uint bindless_slot = environment_bindings.environment_static_slot;

    if (bindless_slot != K_INVALID_BINDLESS_INDEX && BX_IN_GLOBAL_SRV(bindless_slot))
    {
        // Access via global ResourceDescriptorHeap (pure bindless)
        StructuredBuffer<EnvironmentStaticData> env_buffer = ResourceDescriptorHeap[bindless_slot];
        out_data = env_buffer[0];
        return true;
    }

    out_data = (EnvironmentStaticData)0;
    return false;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI

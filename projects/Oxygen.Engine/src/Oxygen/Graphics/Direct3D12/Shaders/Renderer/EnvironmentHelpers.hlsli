//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Renderer/EnvironmentFrameBindings.hlsli"
#include "Renderer/ViewConstants.hlsli"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/ViewFrameBindings.hlsli"

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

/**
 * Converts an Oxygen world-space direction (Z-up) to the direction used for
 * sampling GPU cubemaps (Y-up).
 *
 * Oxygen world convention (see `oxygen::space::move`):
 *   X = right, Y = back, Z = up  (so forward is -Y)
 * GPU cubemap convention used by our cooking/sampling path:
 *   X = right, Y = up, Z = forward
 *
 * Therefore:
 *   gpu.x = oxy.x
 *   gpu.y = oxy.z
 *   gpu.z = -oxy.y
 */
static inline float3 CubemapSamplingDirFromOxygenWS(float3 dir_ws)
{
    // Oxygen Forward (-Y) -> Cubemap Front (+Z)
    // Oxygen Up (+Z)      -> Cubemap Up (+Y)
    return float3(dir_ws.x, dir_ws.z, -dir_ws.y);
}

/**
 * Converts a GPU Cubemap sampling direction (Y-up) back to Oxygen world-space direction (Z-up).
 * This is the inverse of CubemapSamplingDirFromOxygenWS.
 *
 * Use this when you have a direction relative to D3D cubemap faces (e.g. from SkyCapture view)
 * and need the corresponding Oxygen World Space direction.
 *
 * Mapping:
 *   oxy.x = gpu.x
 *   oxy.y = -gpu.z
 *   oxy.z = gpu.y
 */
static inline float3 OxygenDirFromCubemapSamplingDir(float3 dir_d3d)
{
    // Cubemap Front (+Z) -> Oxygen Forward (-Y)
    // Cubemap Up (+Y)    -> Oxygen Up (+Z)
    return float3(dir_d3d.x, -dir_d3d.z, dir_d3d.y);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI

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

static inline float3 EvaluateStaticSkyLightDiffuseSh(
    EnvironmentStaticData env_data,
    float3 normal_ws)
{
    if (env_data.sky_light.enabled == 0u
        || env_data.sky_light.diffuse_sh_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(env_data.sky_light.diffuse_sh_slot)) {
        return 0.0f.xxx;
    }

    const float normal_len_sq = dot(normal_ws, normal_ws);
    if (normal_len_sq <= 1.0e-8f) {
        return 0.0f.xxx;
    }

    StructuredBuffer<float4> sh =
        ResourceDescriptorHeap[env_data.sky_light.diffuse_sh_slot];
    const float4 normal = float4(normal_ws * rsqrt(normal_len_sq), 1.0f);
    const float3 intermediate0 = float3(
        dot(sh[0], normal),
        dot(sh[1], normal),
        dot(sh[2], normal));

    const float4 v_b = normal.xyzz * normal.yzzx;
    const float3 intermediate1 = float3(
        dot(sh[3], v_b),
        dot(sh[4], v_b),
        dot(sh[5], v_b));

    const float v_c = normal.x * normal.x - normal.y * normal.y;
    const float3 intermediate2 = sh[6].xyz * v_c;
    return max(0.0f.xxx, intermediate0 + intermediate1 + intermediate2);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTHELPERS_HLSLI

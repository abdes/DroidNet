//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

struct EnvironmentViewData
{
    uint flags;
    float sky_view_lut_slice;
    float planet_to_sun_cos_zenith;
    float aerial_perspective_distance_scale;
    float aerial_scattering_strength;
    uint3 _pad;
    float4 planet_center_ws_pad;
    float4 planet_up_ws_camera_altitude_m;
};

static EnvironmentViewData LoadEnvironmentViewData(uint slot)
{
    EnvironmentViewData invalid_data = (EnvironmentViewData)0;
    invalid_data.flags = 0u;
    invalid_data.sky_view_lut_slice = 0.0f;
    invalid_data.planet_to_sun_cos_zenith = 0.0f;
    invalid_data.aerial_perspective_distance_scale = 1.0f;
    invalid_data.aerial_scattering_strength = 1.0f;
    // Keep the invalid-view fallback aligned with the C++ default
    // EnvironmentViewData contract: world origin sits on the planet surface and
    // the planet center is below it on -Z.
    invalid_data.planet_center_ws_pad = float4(0.0f, 0.0f, -6360000.0f, 0.0f);
    invalid_data.planet_up_ws_camera_altitude_m = float4(0.0f, 0.0f, 1.0f, 0.0f);

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_data;
    }

    StructuredBuffer<EnvironmentViewData> data_buffer = ResourceDescriptorHeap[slot];
    return data_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI

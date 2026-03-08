//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_SYNTHETICSUNDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_SYNTHETICSUNDATA_HLSLI

struct SyntheticSunData
{
    uint enabled;
    float cos_zenith;
    uint2 _pad;
    float4 direction_ws_illuminance;
    float4 color_rgb_intensity;
};

#endif // OXYGEN_D3D12_SHADERS_RENDERER_SYNTHETICSUNDATA_HLSLI

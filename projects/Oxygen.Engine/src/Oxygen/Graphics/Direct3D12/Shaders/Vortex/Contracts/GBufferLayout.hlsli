//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_GBUFFERLAYOUT_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_GBUFFERLAYOUT_HLSLI

static const uint GBUFFER_NORMAL = 0u;
static const uint GBUFFER_MATERIAL = 1u;
static const uint GBUFFER_BASE_COLOR = 2u;
static const uint GBUFFER_CUSTOM_DATA = 3u;
static const uint GBUFFER_COUNT = 4u;

struct GBufferOutput
{
    float4 gbuffer_normal : SV_Target0;
    float4 gbuffer_material : SV_Target1;
    float4 gbuffer_base_color : SV_Target2;
    float4 gbuffer_custom_data : SV_Target3;
    float4 emissive_scene_color : SV_Target4;
};

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_GBUFFERLAYOUT_HLSLI

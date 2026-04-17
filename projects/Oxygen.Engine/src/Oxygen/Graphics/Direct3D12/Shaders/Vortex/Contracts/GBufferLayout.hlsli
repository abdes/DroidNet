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
static const uint GBUFFER_SHADOW_FACTORS = 4u;
static const uint GBUFFER_WORLD_TANGENT = 5u;
static const uint GBUFFER_ACTIVE_COUNT = 4u;
static const uint GBUFFER_BINDING_COUNT = 6u;

struct GBufferOutput
{
    float4 gbuffer_normal : SV_Target0;
    float4 gbuffer_material : SV_Target1;
    float4 gbuffer_base_color : SV_Target2;
    float4 gbuffer_custom_data : SV_Target3;
    float4 emissive_scene_color : SV_Target4;
#if defined(HAS_VELOCITY)
    float2 velocity : SV_Target5;
#endif
};

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_GBUFFERLAYOUT_HLSLI

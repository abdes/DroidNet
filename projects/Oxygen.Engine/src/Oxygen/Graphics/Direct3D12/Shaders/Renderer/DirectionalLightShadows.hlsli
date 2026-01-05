//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALLIGHTSHADOWS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALLIGHTSHADOWS_HLSLI

#ifndef OXYGEN_MAX_SHADOW_CASCADES
#define OXYGEN_MAX_SHADOW_CASCADES 4
#endif

struct DirectionalLightShadows
{
    uint cascade_count;
    float distribution_exponent;
    float _pad0;
    float _pad1;

    float cascade_distances[OXYGEN_MAX_SHADOW_CASCADES];
    float4x4 cascade_view_proj[OXYGEN_MAX_SHADOW_CASCADES];
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALLIGHTSHADOWS_HLSLI

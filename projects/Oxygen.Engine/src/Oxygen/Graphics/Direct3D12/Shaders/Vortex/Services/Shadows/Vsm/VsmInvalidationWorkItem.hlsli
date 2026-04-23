//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMINVALIDATIONWORKITEM_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMINVALIDATIONWORKITEM_HLSLI

struct VsmShaderInvalidationWorkItem
{
    uint4 primitive;
    float4 world_bounding_sphere;
    uint projection_index;
    uint scope;
    uint matched_static_feedback;
    uint _pad0;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMINVALIDATIONWORKITEM_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_MATERIALTEMPLATEADAPTER_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_MATERIALTEMPLATEADAPTER_HLSLI

#include "GBufferMaterialOutput.hlsli"

struct BasePassMaterialTemplateInput
{
    float3 world_pos;
    float3 world_normal;
    float3 world_tangent;
    float3 world_bitangent;
    float4 uvs;
    uint draw_index;
    uint is_front_face;
};

static inline GBufferOutput EvaluateBasePassMaterialOutput(
    BasePassMaterialTemplateInput input)
{
    return EvaluateGBufferMaterialOutput(input.world_pos, input.world_normal,
        input.world_tangent, input.world_bitangent, input.uvs,
        input.draw_index, input.is_front_face != 0u);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_MATERIALTEMPLATEADAPTER_HLSLI

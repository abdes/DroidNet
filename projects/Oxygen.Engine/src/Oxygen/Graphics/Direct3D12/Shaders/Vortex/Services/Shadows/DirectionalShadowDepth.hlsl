//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Contracts/Draw/Vertex.hlsli"

#define BX_VERTEX_TYPE Vertex
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct ShadowPassConstants
{
    float4x4 light_view_projection;
    float depth_bias;
    float normal_bias;
    uint draw_metadata_slot;
    uint current_worlds_slot;
    uint instance_data_slot;
    float _padding1;
};

struct ShadowDepthVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static inline ShadowPassConstants LoadShadowPassConstants(uint slot)
{
    ShadowPassConstants constants = (ShadowPassConstants)0;
    constants.light_view_projection = float4x4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    if (slot == K_INVALID_BINDLESS_INDEX) {
        return constants;
    }

    ConstantBuffer<ShadowPassConstants> constants_buffer =
        ResourceDescriptorHeap[slot];
    return constants_buffer;
}

[shader("vertex")]
ShadowDepthVSOutput VortexShadowDepthVS(
    uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    ShadowDepthVSOutput output = (ShadowDepthVSOutput)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);

    const ShadowPassConstants pass_constants =
        LoadShadowPassConstants(g_PassConstantsIndex);

    DrawMetadata metadata;
    if (!BX_LoadDrawMetadata(
            pass_constants.draw_metadata_slot, g_DrawIndex, metadata)) {
        return output;
    }

    const uint actual_vertex_index =
        BX_ResolveVertexIndex(metadata, vertex_id);
    const Vertex vertex =
        BX_LoadVertex(metadata.vertex_buffer_index, actual_vertex_index);
    output.uv = vertex.texcoord;

    const float4x4 world_matrix = BX_LoadInstanceWorldMatrix(
        pass_constants.current_worlds_slot, pass_constants.instance_data_slot, metadata,
        instance_id);
    float4 world_position = mul(world_matrix, float4(vertex.position, 1.0f));
    output.position = mul(pass_constants.light_view_projection, world_position);
    output.position.z -= pass_constants.depth_bias;
    return output;
}

[shader("pixel")]
void VortexShadowDepthMaskedPS(ShadowDepthVSOutput input)
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif
}

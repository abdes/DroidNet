//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaskedAlphaTest.hlsli"
#include "Renderer/Vertex.hlsli"

#define BX_VERTEX_TYPE Vertex
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct DepthPrepassVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 EncodeStaticVelocity(float4 clip_position)
{
    const float safe_w = max(abs(clip_position.w), 1.0e-5f);
    const float2 ndc_position = clip_position.xy / safe_w;
    return ndc_position - ndc_position;
}

[shader("vertex")]
DepthPrepassVSOutput DepthPrepassVS(
    uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    DepthPrepassVSOutput output = (DepthPrepassVSOutput)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);

    const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();

    DrawMetadata metadata;
    if (!BX_LoadDrawMetadata(
            draw_bindings.draw_metadata_slot, g_DrawIndex, metadata)) {
        return output;
    }

    const uint actual_vertex_index
        = BX_ResolveVertexIndex(metadata, vertex_id);
    const Vertex vertex
        = BX_LoadVertex(metadata.vertex_buffer_index, actual_vertex_index);
    output.uv = vertex.texcoord;

    const float4x4 world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.transforms_slot, draw_bindings.instance_data_slot, metadata,
        instance_id);
    const float4 world_position
        = mul(world_matrix, float4(vertex.position, 1.0f));
    const float4 view_position = mul(view_matrix, world_position);
    output.position = mul(projection_matrix, view_position);
    return output;
}

#ifdef HAS_VELOCITY
struct DepthPrepassPSOutput
{
    float2 velocity : SV_Target0;
};

[shader("pixel")]
DepthPrepassPSOutput DepthPrepassPS(DepthPrepassVSOutput input)
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif

    DepthPrepassPSOutput output;
    output.velocity = EncodeStaticVelocity(input.position);
    return output;
}
#else
[shader("pixel")]
void DepthPrepassPS(DepthPrepassVSOutput input)
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif
}
#endif

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
    float4 shadow_bias_parameters;
    float4 light_direction_to_source;
    float4 light_position_and_inv_range;
    uint draw_metadata_slot;
    uint current_worlds_slot;
    uint instance_data_slot;
    uint _padding0;
};

struct ShadowDepthVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float biased_depth : TEXCOORD1;
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
    const float4 unbiased_position = output.position;
    const float3 world_normal_unnormalized =
        mul((float3x3)world_matrix, vertex.normal);
    const float3 world_normal =
        dot(world_normal_unnormalized, world_normal_unnormalized) > 1.0e-8f
            ? normalize(world_normal_unnormalized)
            : float3(0.0f, 1.0f, 0.0f);
    const float no_l = abs(dot(
        normalize(pass_constants.light_direction_to_source.xyz), world_normal));
    const float max_slope_depth_bias = pass_constants.shadow_bias_parameters.z;
    const float slope = clamp(
        no_l > 1.0e-4f
            ? sqrt(saturate(1.0f - no_l * no_l)) / no_l
            : max_slope_depth_bias,
        0.0f, max_slope_depth_bias);
    const float depth_bias = pass_constants.shadow_bias_parameters.x
        + pass_constants.shadow_bias_parameters.y * slope;
    if (pass_constants.light_position_and_inv_range.w > 0.0f) {
        const float3 light_to_vertex =
            world_position.xyz - pass_constants.light_position_and_inv_range.xyz;
        const float axial_distance = max(
            0.0f,
            dot(light_to_vertex, normalize(pass_constants.light_direction_to_source.xyz)));
        output.biased_depth =
            1.0f - axial_distance * pass_constants.light_position_and_inv_range.w
            - depth_bias;
    } else {
        output.position.z -= depth_bias;
        output.biased_depth = output.position.z / max(abs(output.position.w), 1.0e-6f);
    }
    // Bias must not move SV_Position before rasterization, especially for
    // perspective local-light shadows near the far end of the cone.
    output.position = unbiased_position;
    return output;
}

[shader("pixel")]
void VortexShadowDepthMaskedPS(ShadowDepthVSOutput input, out float out_depth : SV_Depth)
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif
    out_depth = saturate(input.biased_depth);
}

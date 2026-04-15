//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/Vertex.hlsli"

#include "Vortex/Materials/MaterialTemplateAdapter.hlsli"

#define BX_VERTEX_TYPE Vertex
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct BasePassGBufferVSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float3 world_bitangent : BINORMAL;
};

[shader("vertex")]
BasePassGBufferVSOutput BasePassGBufferVS(
    uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    BasePassGBufferVSOutput output = (BasePassGBufferVSOutput)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.world_normal = float3(0.0f, 0.0f, 1.0f);
    output.world_tangent = float3(1.0f, 0.0f, 0.0f);
    output.world_bitangent = float3(0.0f, 1.0f, 0.0f);
    output.color = float3(1.0f, 1.0f, 1.0f);

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

    const float4x4 world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.transforms_slot, draw_bindings.instance_data_slot, metadata,
        instance_id);
    const uint transform_index = BX_ResolveTransformIndex(
        metadata, draw_bindings.instance_data_slot, instance_id);

    float3x3 normal_matrix = (float3x3)world_matrix;
    if (draw_bindings.normal_matrices_slot != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<float4x4> normal_matrices
            = ResourceDescriptorHeap[draw_bindings.normal_matrices_slot];
        normal_matrix = (float3x3)normal_matrices[transform_index];
    }

    const float3x3 world_basis = (float3x3)world_matrix;
    const float4 world_position
        = mul(world_matrix, float4(vertex.position, 1.0f));
    const float4 view_position = mul(view_matrix, world_position);

    float3 world_normal = SafeNormalize(mul(normal_matrix, vertex.normal));
    if (dot(world_normal, world_normal) < 0.5f) {
        world_normal = float3(0.0f, 0.0f, 1.0f);
    }

    float3 world_tangent = mul(world_basis, vertex.tangent);
    world_tangent -= world_normal * dot(world_normal, world_tangent);
    if (dot(world_tangent, world_tangent) <= 1e-6f) {
        const float3 axis = abs(world_normal.z) > 0.9f
            ? float3(1.0f, 0.0f, 0.0f)
            : float3(0.0f, 0.0f, 1.0f);
        world_tangent = cross(world_normal, axis);
    }
    world_tangent = SafeNormalize(world_tangent);

    float3 world_bitangent = SafeNormalize(cross(world_normal, world_tangent));
    if (dot(world_bitangent, world_bitangent) < 0.5f) {
        world_bitangent = float3(0.0f, 1.0f, 0.0f);
    }

    output.position = mul(projection_matrix, view_position);
    output.color = vertex.color.rgb;
    output.uv = vertex.texcoord;
    output.world_pos = world_position.xyz;
    output.world_normal = world_normal;
    output.world_tangent = world_tangent;
    output.world_bitangent = world_bitangent;
    return output;
}

[shader("pixel")]
GBufferOutput BasePassGBufferPS(
    BasePassGBufferVSOutput input, bool is_front_face : SV_IsFrontFace)
{
    BasePassMaterialTemplateInput material_input;
    material_input.world_pos = input.world_pos;
    material_input.world_normal = input.world_normal;
    material_input.world_tangent = input.world_tangent;
    material_input.world_bitangent = input.world_bitangent;
    material_input.uv0 = input.uv;
    material_input.draw_index = g_DrawIndex;
    material_input.is_front_face = is_front_face ? 1u : 0u;
    return EvaluateBasePassMaterialOutput(material_input);
}

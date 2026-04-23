//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Draw/DrawHelpers.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Draw/Vertex.hlsli"

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
    float4 current_clip_position : TEXCOORD2;
    float4 previous_clip_position : TEXCOORD3;
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
        draw_bindings.current_worlds_slot, draw_bindings.instance_data_slot, metadata,
        instance_id);
    const float4x4 previous_world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.previous_worlds_slot, draw_bindings.instance_data_slot, metadata,
        instance_id);
    const uint transform_index = BX_ResolveTransformIndex(
        metadata, draw_bindings.instance_data_slot, instance_id);
    VelocityDrawMetadata velocity_metadata = MakeInvalidVelocityDrawMetadata();
    LoadVelocityDrawMetadata(
        draw_bindings.velocity_draw_metadata_slot, g_DrawIndex, velocity_metadata);
    const ViewHistoryFrameBindings view_history
        = LoadResolvedViewHistoryFrameBindings();

    float3x3 normal_matrix = (float3x3)world_matrix;
    if (draw_bindings.normal_matrices_slot != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<float4x4> normal_matrices
            = ResourceDescriptorHeap[draw_bindings.normal_matrices_slot];
        normal_matrix = (float3x3)normal_matrices[transform_index];
    }

    const float3x3 world_basis = (float3x3)world_matrix;
    const float3 current_material_wpo_offset
        = ResolveCurrentMaterialWpoOffset(draw_bindings, velocity_metadata);
    const float3 previous_material_wpo_offset
        = ResolvePreviousMaterialWpoOffset(draw_bindings, velocity_metadata);
    float4 world_position
        = mul(world_matrix, float4(vertex.position, 1.0f));
    world_position.xyz += current_material_wpo_offset;
    float4 previous_world_position
        = mul(previous_world_matrix, float4(vertex.position, 1.0f));
    previous_world_position.xyz += previous_material_wpo_offset;
    const float4 view_position = mul(view_matrix, world_position);
    float4 previous_view_position
        = mul(view_history.previous_view_matrix, previous_world_position);

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
    output.current_clip_position = output.position;
    output.previous_clip_position = mul(
        view_history.previous_projection_matrix, previous_view_position);
    if ((view_history.validity_flags & VIEW_HISTORY_FLAG_PREVIOUS_VIEW_VALID) == 0u) {
        output.previous_clip_position = output.current_clip_position;
    }
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
    GBufferOutput output = EvaluateBasePassMaterialOutput(material_input);
#if defined(HAS_VELOCITY)
    const float current_w = max(abs(input.current_clip_position.w), 1.0e-5f);
    const float previous_w = max(abs(input.previous_clip_position.w), 1.0e-5f);
    const float2 current_ndc = input.current_clip_position.xy / current_w;
    const float2 previous_ndc = input.previous_clip_position.xy / previous_w;
    output.velocity = current_ndc - previous_ndc;
#endif
    return output;
}

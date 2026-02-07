//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardMesh_VS.hlsl
//! @brief Shared vertex shader for Forward+ mesh rendering.
//!
//! This vertex shader is used by both the main ForwardMesh_PS.hlsl and any
//! debug visualizations. It handles:
//! - Bindless vertex/index buffer access via DrawMetadata
//! - World/view/projection transforms
//! - Normal matrix transforms for correct lighting
//! - Per-instance transform lookups for instanced rendering

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"
#include "Renderer/Vertex.hlsli"

#include "Core/Bindless/BindlessHelpers.hlsl"
#include "Forward/ForwardPbr.hlsli"

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Vertex shader output / Pixel shader input
struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float3 world_bitangent : BINORMAL;
};

[shader("vertex")]
VSOutput VS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output;

    // Access per-draw metadata buffer through dynamic slot; skip if unavailable.
    if (bindless_draw_metadata_slot == K_INVALID_BINDLESS_INDEX) {
        // Fallback: no geometry; output origin.
        output.position = float4(0,0,0,1);
        output.color = float3(1,0,1); // debug magenta
        output.uv = float2(0,0);
        output.world_pos = float3(0,0,0);
        output.world_normal = float3(0,0,1);   // Z is up
        output.world_tangent = float3(1,0,0);
        output.world_bitangent = float3(0,1,0);
        return output;
    }
    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
    // Select per-draw entry using the draw index provided via root constant
    DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

    uint vertex_buffer_index = meta.vertex_buffer_index;
    uint index_buffer_index = meta.index_buffer_index;

    uint actual_vertex_index;
    if (meta.is_indexed) {
        // For indexed rendering, get the index buffer and read the actual vertex index
        StructuredBuffer<uint> index_buffer = ResourceDescriptorHeap[index_buffer_index];
        actual_vertex_index = index_buffer[meta.first_index + vertexID] + (uint)meta.base_vertex;
    } else {
        // For non-indexed rendering, use the vertex ID directly
        actual_vertex_index = vertexID + (uint)meta.base_vertex;
    }

    // Access vertex data using ResourceDescriptorHeap
    StructuredBuffer<Vertex> vertex_buffer = ResourceDescriptorHeap[vertex_buffer_index];
    Vertex vertex = vertex_buffer[actual_vertex_index];

    // Resolve per-instance transform index
    // For instanced draws (instance_count > 1), fetch transform index from instance data buffer.
    // For single-instance draws, use transform_index directly from DrawMetadata.
    uint transform_index = meta.transform_index;
    if (meta.instance_count > 1 && bindless_instance_data_slot != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<uint> instance_data = ResourceDescriptorHeap[bindless_instance_data_slot];
        transform_index = instance_data[meta.instance_metadata_offset + instanceID];
    }

    // Fetch per-draw world matrix and apply world, view, and projection transforms
    float4x4 world_matrix;
    if (bindless_transforms_slot != 0xFFFFFFFFu) {
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[bindless_transforms_slot];
        // Use per-instance transform index
        world_matrix = worlds[transform_index];
    } else {
        world_matrix = float4x4(1,0,0,0,
                                0,1,0,0,
                                0,0,1,0,
                                0,0,0,1);
    }
    float4 world_pos = mul(world_matrix, float4(vertex.position, 1.0));
    // Compute world-space normal: prefer uploaded normal matrices if available
    float3x3 normal_mat;
    if (bindless_normal_matrices_slot != 0xFFFFFFFFu) {
        StructuredBuffer<float4x4> normals = ResourceDescriptorHeap[bindless_normal_matrices_slot];
        normal_mat = (float3x3)normals[transform_index];
    } else {
        normal_mat = (float3x3)world_matrix;
    }
    const float3x3 world_3x3 = (float3x3)world_matrix;
    float3 n_ws = SafeNormalize(mul(normal_mat, vertex.normal));
    // Tangent-space basis (TBN): transform tangent/bitangent as direction
    // vectors with the world matrix, then orthonormalize here so the pixel
    // shader can stay on a cheap normalize-only path.
    float3 t_ws_in = mul(world_3x3, vertex.tangent);
    float3 b_ws_in = mul(world_3x3, vertex.bitangent);

    // Ensure a valid geometric normal.
    float3 NN = n_ws;
    if (dot(NN, NN) < 0.5) {
        NN = float3(0.0, 1.0, 0.0);
    }

    // Gram-Schmidt orthonormalization for tangent.
    float3 T = t_ws_in - NN * dot(NN, t_ws_in);
    if (dot(T, T) <= 1e-6) {
        const float3 axis = (abs(NN.y) > 0.9) ? float3(1.0, 0.0, 0.0)
                                              : float3(0.0, 1.0, 0.0);
        T = cross(NN, axis);
    }
    T = SafeNormalize(T);
    if (dot(T, T) < 0.5) {
        T = float3(1.0, 0.0, 0.0);
    }

    // Derive bitangent from cross to keep orthogonality and preserve handedness
    // using the incoming bitangent when available.
    const float3 B_from_cross = cross(NN, T);
    const float handedness = (dot(B_from_cross, b_ws_in) < 0.0) ? -1.0 : 1.0;
    float3 B = SafeNormalize(B_from_cross * handedness);
    if (dot(B, B) < 0.5) {
        B = cross(NN, T);
    }
    float4 view_pos = mul(view_matrix, world_pos);
    float4 proj_pos = mul(projection_matrix, view_pos);
    output.position = proj_pos;
    output.color = vertex.color.rgb;
    output.uv = vertex.texcoord;
    output.world_pos = world_pos.xyz;
    output.world_normal = NN;
    output.world_tangent = T;
    output.world_bitangent = B;
    return output;
}

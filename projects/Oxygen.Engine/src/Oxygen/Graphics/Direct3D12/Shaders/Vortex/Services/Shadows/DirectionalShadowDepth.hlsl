//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Shadow depth has no camera view: every input comes from the pass constants.

#include "Vortex/Contracts/Draw/DrawFrameBindings.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Draw/MeshPosition.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Contracts/Draw/Vertex.hlsli"
#include "Vortex/Contracts/Shadows/ShadowCascadeBinding.hlsli"

#define BX_VERTEX_TYPE Vertex
#include "Core/Bindless/BindlessHelpers.hlsl"

// Mesh shaders take their draw index from SV_StartInstanceLocation (see
// Vortex/Contracts/Draw/DrawHelpers.hlsli); they read only the pass constants
// root constant.
cbuffer RootConstants : register(b2, space0)
{
    uint g_PassConstantsIndex : packoffset(c0.y);
}

struct ShadowPassConstants
{
    float4x4 light_view_projection;
    float4 shadow_bias_parameters;
    float4 light_direction_to_source;
    float4 light_position_and_inv_range;
    // The view's published DrawFrameBindings: draws, transforms, materials
    // and world-position offsets, exactly as the camera passes see them.
    uint draw_frame_slot;
    // A directional cascade fitted on the GPU: its record replaces
    // light_view_projection.
    uint cascade_records_srv;
    uint cascade_index;
    uint _pad0;
};

struct ShadowDepthVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
#if !defined(CUBE_SHADOW)
    float biased_depth : TEXCOORD1;
#endif
    nointerpolation uint draw_index : DRAW_INDEX;
};

static inline bool TryLoadShadowPassConstants(out ShadowPassConstants constants)
{
    constants = (ShadowPassConstants)0;
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return false;
    }

    StructuredBuffer<ShadowPassConstants> constants_buffer =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    constants = constants_buffer[0];
    if (constants.cascade_records_srv != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<VortexShadowCascadeBinding> cascades
            = ResourceDescriptorHeap[constants.cascade_records_srv];
        constants.light_view_projection
            = cascades[constants.cascade_index].light_view_projection;
    }
    return constants.draw_frame_slot != K_INVALID_BINDLESS_INDEX;
}

[shader("vertex")]
ShadowDepthVSOutput VortexShadowDepthVS(uint vertex_id : SV_VertexID,
    uint instance_id : SV_InstanceID,
    uint draw_index : SV_StartInstanceLocation)
{
    ShadowDepthVSOutput output = (ShadowDepthVSOutput)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.draw_index = draw_index;

    ShadowPassConstants pass_constants;
    if (!TryLoadShadowPassConstants(pass_constants)) {
        return output;
    }
    const DrawFrameBindings draw_bindings
        = LoadDrawFrameBindings(pass_constants.draw_frame_slot);

    DrawMetadata metadata;
    if (!BX_LoadDrawMetadata(
            draw_bindings.draw_metadata_slot, draw_index, metadata)) {
        return output;
    }

    const uint actual_vertex_index =
        BX_ResolveVertexIndex(metadata, vertex_id);
    const Vertex vertex =
        BX_LoadVertex(metadata.vertex_buffer_index, actual_vertex_index);
    output.uv = vertex.texcoord;

    float4x4 world_matrix;
    if (!BX_TryLoadInstanceWorldMatrix(
            draw_bindings.current_worlds_slot, draw_bindings.instance_data_slot,
            metadata, instance_id, world_matrix)) {
        return output;
    }
    const float4 world_position = ComputeMeshWorldPosition(world_matrix,
        vertex.position, ResolveDrawWorldOffset(draw_bindings, draw_index));
    output.position = mul(pass_constants.light_view_projection, world_position);
#if !defined(CUBE_SHADOW)
    float slope_bias = 0.0f;
    if (pass_constants.shadow_bias_parameters.y > 0.0f) {
        // Reuse the inverse-transpose publication consumed by the base pass.
        // Multiplying normals by world is incorrect under nonuniform scale.
        uint transform_index = 0u;
        float4x4 normal_transform;
        if (!BX_TryResolveTransformIndex(metadata,
                draw_bindings.instance_data_slot, instance_id, transform_index)
            || !BX_TryLoadMatrix(draw_bindings.normal_matrices_slot,
                transform_index, normal_transform)) {
            return output;
        }
        const float3x3 normal_matrix = (float3x3)normal_transform;
        const float3 transformed_normal = mul(normal_matrix, vertex.normal);
        const float3 world_normal = dot(transformed_normal, transformed_normal) > 1.0e-8f
            ? normalize(transformed_normal) : float3(0.0f, 1.0f, 0.0f);
        const float no_l = abs(dot(
            normalize(pass_constants.light_direction_to_source.xyz), world_normal));
        const float max_slope = pass_constants.shadow_bias_parameters.z;
        const float slope = clamp(no_l > 1.0e-4f
            ? sqrt(saturate(1.0f - no_l * no_l)) / no_l : max_slope,
            0.0f, max_slope);
        slope_bias = pass_constants.shadow_bias_parameters.y * slope;
    }
    const float depth_bias = pass_constants.shadow_bias_parameters.x
        + slope_bias;
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
        // Bias the written depth only: moving SV_Position before
        // rasterization would shift coverage, especially for perspective
        // local-light shadows near the far end of the cone.
        const float biased_z = output.position.z - depth_bias;
        output.biased_depth = biased_z / max(abs(output.position.w), 1.0e-6f);
    }
#endif
    return output;
}

[shader("pixel")]
void VortexShadowDepthMaskedPS(ShadowDepthVSOutput input
#if !defined(CUBE_SHADOW)
    , out float out_depth : SV_Depth
#endif
)
{
#if defined(ALPHA_TEST)
    // The casters, like the vertex shader's, come from the view's draws.
    ShadowPassConstants pass_constants;
    if (!TryLoadShadowPassConstants(pass_constants)) {
        discard;
    }
    const DrawFrameBindings draw_bindings
        = LoadDrawFrameBindings(pass_constants.draw_frame_slot);
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(EvaluateMaskedAlphaTestFromSlots(input.uv,
        draw_bindings.draw_metadata_slot,
        draw_bindings.material_shading_constants_slot, input.draw_index,
        linear_sampler));
#endif
#if !defined(CUBE_SHADOW)
    out_depth = saturate(input.biased_depth);
#endif
}

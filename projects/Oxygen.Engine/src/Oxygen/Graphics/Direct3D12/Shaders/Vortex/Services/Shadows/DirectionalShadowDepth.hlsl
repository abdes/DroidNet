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
#include "Vortex/Contracts/Shadows/Generated.ShadowContracts.hlsli"
#include "Vortex/Contracts/Shadows/Generated.ShadowPassContracts.hlsli"

#define BX_VERTEX_TYPE Vertex
#include "Core/Bindless/BindlessHelpers.hlsl"

// Mesh shaders take their draw index from SV_StartInstanceLocation (see
// Vortex/Contracts/Draw/DrawHelpers.hlsli); they read only the pass constants
// root constant.
cbuffer RootConstants : register(b2, space0)
{
    uint g_PassConstantsIndex : packoffset(c0.y);
}

struct ShadowDepthVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
#if !defined(CUBE_SHADOW)
    float biased_depth : TEXCOORD1;
#endif
    nointerpolation uint draw_index : DRAW_INDEX;
};

// The pass constants, with a GPU-fitted cascade's matrix in place of the CPU
// one; false when the constants or their draw routing are missing.
static inline bool TryLoadShadowDepthInputs(out ShadowPassConstants constants)
{
    if (!TryLoadShadowPassConstants(g_PassConstantsIndex, constants)) {
        return false;
    }
    if (constants.cascade_records_srv != K_INVALID_BINDLESS_INDEX) {
        VortexShadowCascadeBinding cascade;
        if (!TryLoadVortexShadowCascadeBinding(
                constants.cascade_records_srv, constants.cascade_index, cascade)) {
            return false;
        }
        constants.light_view_projection = cascade.light_view_projection;
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
    if (!TryLoadShadowDepthInputs(pass_constants)) {
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
    if (!TryLoadShadowDepthInputs(pass_constants)) {
        discard;
    }
    const DrawFrameBindings draw_bindings
        = LoadDrawFrameBindings(pass_constants.draw_frame_slot);
    const SamplerState linear_sampler = SamplerDescriptorHeap[K_SAMPLER_LINEAR_WRAP];
    ApplyMaskedAlphaClip(EvaluateMaskedAlphaTestFromSlots(input.uv,
        draw_bindings.draw_metadata_slot,
        draw_bindings.material_shading_constants_slot, input.draw_index,
        linear_sampler));
#endif
#if !defined(CUBE_SHADOW)
    out_depth = saturate(input.biased_depth);
#endif
}

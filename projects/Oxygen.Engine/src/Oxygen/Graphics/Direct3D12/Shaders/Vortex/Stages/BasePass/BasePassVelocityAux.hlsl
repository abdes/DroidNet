//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/Draw/DrawHelpers.hlsli"
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

struct BasePassVelocityAuxVSOutput
{
    float4 position : SV_POSITION;
    float4 base_current_clip_position : TEXCOORD0;
    float4 base_previous_clip_position : TEXCOORD1;
    float4 mv_current_clip_position : TEXCOORD2;
    float4 mv_previous_clip_position : TEXCOORD3;
    float2 uv : TEXCOORD4;
};

static float2 ClipToNdc(float4 clip_position)
{
    const float safe_w = max(abs(clip_position.w), 1.0e-5f);
    return clip_position.xy / safe_w;
}

[shader("vertex")]
BasePassVelocityAuxVSOutput BasePassVelocityAuxVS(
    uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    BasePassVelocityAuxVSOutput output = (BasePassVelocityAuxVSOutput)0;
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
        draw_bindings.current_worlds_slot, draw_bindings.instance_data_slot, metadata,
        instance_id);
    const float4x4 previous_world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.previous_worlds_slot, draw_bindings.instance_data_slot, metadata,
        instance_id);
    VelocityDrawMetadata velocity_metadata = MakeInvalidVelocityDrawMetadata();
    LoadVelocityDrawMetadata(
        draw_bindings.velocity_draw_metadata_slot, g_DrawIndex, velocity_metadata);
    const ViewHistoryFrameBindings view_history
        = LoadResolvedViewHistoryFrameBindings();

    float4 base_current_world_position
        = mul(world_matrix, float4(vertex.position, 1.0f));
    float4 base_previous_world_position
        = mul(previous_world_matrix, float4(vertex.position, 1.0f));
    base_current_world_position.xyz += ResolveCurrentMaterialWpoOffset(
        draw_bindings, velocity_metadata);
    base_previous_world_position.xyz += ResolvePreviousMaterialWpoOffset(
        draw_bindings, velocity_metadata);

    float4 mv_current_world_position = base_current_world_position;
    float4 mv_previous_world_position = base_previous_world_position;
    mv_current_world_position.xyz += ResolveCurrentMotionVectorWorldOffset(
        draw_bindings, velocity_metadata);
    mv_previous_world_position.xyz += ResolvePreviousMotionVectorWorldOffset(
        draw_bindings, velocity_metadata);

    output.position
        = mul(projection_matrix, mul(view_matrix, base_current_world_position));
    output.base_current_clip_position = output.position;
    output.base_previous_clip_position = mul(
        view_history.previous_projection_matrix,
        mul(view_history.previous_view_matrix, base_previous_world_position));
    output.mv_current_clip_position
        = mul(projection_matrix, mul(view_matrix, mv_current_world_position));
    output.mv_previous_clip_position = mul(
        view_history.previous_projection_matrix,
        mul(view_history.previous_view_matrix, mv_previous_world_position));

    if ((view_history.validity_flags & VIEW_HISTORY_FLAG_PREVIOUS_VIEW_VALID) == 0u) {
        output.base_previous_clip_position = output.base_current_clip_position;
        output.mv_previous_clip_position = output.mv_current_clip_position;
    }

    return output;
}

[shader("pixel")]
float2 BasePassVelocityAuxPS(BasePassVelocityAuxVSOutput input) : SV_Target0
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif

    const float2 base_current_ndc = ClipToNdc(input.base_current_clip_position);
    const float2 base_previous_ndc = ClipToNdc(input.base_previous_clip_position);
    const float2 mv_current_ndc = ClipToNdc(input.mv_current_clip_position);
    const float2 mv_previous_ndc = ClipToNdc(input.mv_previous_clip_position);
    return (mv_current_ndc - mv_previous_ndc)
        - (base_current_ndc - base_previous_ndc);
}

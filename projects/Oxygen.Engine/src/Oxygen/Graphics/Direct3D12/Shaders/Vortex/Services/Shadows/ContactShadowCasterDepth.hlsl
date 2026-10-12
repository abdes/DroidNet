//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Contact-shadow caster depth: camera-space depth of the shadow casters.
//
// The pass records before the camera's view constants are final, so it reads
// only its inputs record: the camera matrices current at record time and the
// view's draw publication.

#include "Vortex/Contracts/Draw/DrawFrameBindings.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Draw/VelocityPublications.hlsli"
#include "Vortex/Contracts/Draw/Vertex.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Shared/MeshClipPosition.hlsli"

#define BX_VERTEX_TYPE Vertex
#include "Core/Bindless/BindlessHelpers.hlsl"

// Mesh shaders take their draw index from SV_StartInstanceLocation (see
// Vortex/Contracts/Draw/DrawHelpers.hlsli); they read only the pass constants
// root constant.
cbuffer RootConstants : register(b2, space0)
{
    uint g_PassConstantsIndex : packoffset(c0.y);
}

// Mirrors ContactShadowCasterDepthPassConstants in
// ContactShadowCasterDepthPass.cpp.
struct ContactShadowCasterDepthPassConstants
{
    float4x4 view_matrix;
    float4x4 projection_matrix;
    uint draw_frame_slot;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct ContactShadowCasterDepthVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint draw_index : DRAW_INDEX;
};

static inline bool TryLoadPassConstants(
    out ContactShadowCasterDepthPassConstants constants)
{
    constants = (ContactShadowCasterDepthPassConstants)0;
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return false;
    }
    StructuredBuffer<ContactShadowCasterDepthPassConstants> buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    constants = buffer[0];
    return constants.draw_frame_slot != K_INVALID_BINDLESS_INDEX;
}

[shader("vertex")]
ContactShadowCasterDepthVSOutput VortexContactShadowCasterDepthVS(
    uint vertex_id : SV_VertexID,
    uint instance_id : SV_InstanceID,
    uint draw_index : SV_StartInstanceLocation)
{
    ContactShadowCasterDepthVSOutput output
        = (ContactShadowCasterDepthVSOutput)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.draw_index = draw_index;

    ContactShadowCasterDepthPassConstants pass_constants;
    if (!TryLoadPassConstants(pass_constants)) {
        return output;
    }
    const DrawFrameBindings draw_bindings
        = LoadDrawFrameBindings(pass_constants.draw_frame_slot);

    DrawMetadata metadata;
    if (!BX_LoadDrawMetadata(
            draw_bindings.draw_metadata_slot, draw_index, metadata)) {
        return output;
    }

    const uint actual_vertex_index
        = BX_ResolveVertexIndex(metadata, vertex_id);
    const Vertex vertex
        = BX_LoadVertex(metadata.vertex_buffer_index, actual_vertex_index);
    output.uv = vertex.texcoord;
    VelocityDrawMetadata velocity_metadata = MakeInvalidVelocityDrawMetadata();
    LoadVelocityDrawMetadata(
        draw_bindings.velocity_draw_metadata_slot, draw_index, velocity_metadata);

    const float4x4 world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.current_worlds_slot, draw_bindings.instance_data_slot,
        metadata, instance_id);
    output.position = ComputeMeshClipPosition(world_matrix, vertex.position,
        ResolveCurrentMaterialWpoOffset(draw_bindings, velocity_metadata),
        pass_constants.view_matrix, pass_constants.projection_matrix);
    return output;
}

[shader("pixel")]
void VortexContactShadowCasterDepthPS(ContactShadowCasterDepthVSOutput input)
{
#if defined(ALPHA_TEST)
    ContactShadowCasterDepthPassConstants pass_constants;
    if (!TryLoadPassConstants(pass_constants)) {
        return;
    }
    const DrawFrameBindings draw_bindings
        = LoadDrawFrameBindings(pass_constants.draw_frame_slot);
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(EvaluateMaskedAlphaTestFromSlots(input.uv,
        draw_bindings.draw_metadata_slot,
        draw_bindings.material_shading_constants_slot, input.draw_index,
        linear_sampler));
#endif
}

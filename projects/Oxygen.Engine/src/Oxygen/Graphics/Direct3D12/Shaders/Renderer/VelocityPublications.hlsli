//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VELOCITYPUBLICATIONS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VELOCITYPUBLICATIONS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Renderer/DrawFrameBindings.hlsli"

#define MOTION_PUBLICATION_CAPABILITY_USES_WORLD_POSITION_OFFSET (1u << 0u)
#define MOTION_PUBLICATION_CAPABILITY_USES_MOTION_VECTOR_WORLD_OFFSET (1u << 1u)
#define MOTION_PUBLICATION_CAPABILITY_USES_TEMPORAL_RESPONSIVENESS (1u << 2u)
#define MOTION_PUBLICATION_CAPABILITY_HAS_PIXEL_ANIMATION (1u << 3u)
#define MOTION_PUBLICATION_CAPABILITY_HAS_RUNTIME_PAYLOAD (1u << 4u)

#define VELOCITY_DRAW_PUBLICATION_FLAG_CURRENT_MATERIAL_WPO_VALID (1u << 4u)
#define VELOCITY_DRAW_PUBLICATION_FLAG_PREVIOUS_MATERIAL_WPO_VALID (1u << 5u)
#define VELOCITY_DRAW_PUBLICATION_FLAG_MATERIAL_WPO_HISTORY_VALID (1u << 6u)
#define VELOCITY_DRAW_PUBLICATION_FLAG_CURRENT_MOTION_VECTOR_STATUS_VALID (1u << 7u)
#define VELOCITY_DRAW_PUBLICATION_FLAG_PREVIOUS_MOTION_VECTOR_STATUS_VALID (1u << 8u)
#define VELOCITY_DRAW_PUBLICATION_FLAG_MOTION_VECTOR_STATUS_HISTORY_VALID (1u << 9u)

struct MaterialWpoPublication
{
    uint2 contract_hash;
    uint capability_flags;
    uint reserved0;
    float4 parameter_block0;
};

struct MotionVectorStatusPublication
{
    uint2 contract_hash;
    uint capability_flags;
    uint reserved0;
    float4 parameter_block0;
};

struct VelocityDrawMetadata
{
    uint current_skinned_pose_index;
    uint previous_skinned_pose_index;
    uint current_morph_index;
    uint previous_morph_index;
    uint current_material_wpo_index;
    uint previous_material_wpo_index;
    uint current_motion_vector_status_index;
    uint previous_motion_vector_status_index;
    uint publication_flags;
    uint pad0;
    uint pad1;
    uint pad2;
};

static inline MaterialWpoPublication MakeInvalidMaterialWpoPublication()
{
    MaterialWpoPublication publication = (MaterialWpoPublication)0;
    publication.contract_hash = uint2(0u, 0u);
    publication.capability_flags = 0u;
    publication.reserved0 = 0u;
    publication.parameter_block0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return publication;
}

static inline MotionVectorStatusPublication MakeInvalidMotionVectorStatusPublication()
{
    MotionVectorStatusPublication publication = (MotionVectorStatusPublication)0;
    publication.contract_hash = uint2(0u, 0u);
    publication.capability_flags = 0u;
    publication.reserved0 = 0u;
    publication.parameter_block0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return publication;
}

static inline VelocityDrawMetadata MakeInvalidVelocityDrawMetadata()
{
    VelocityDrawMetadata metadata = (VelocityDrawMetadata)0;
    metadata.current_skinned_pose_index = K_INVALID_BINDLESS_INDEX;
    metadata.previous_skinned_pose_index = K_INVALID_BINDLESS_INDEX;
    metadata.current_morph_index = K_INVALID_BINDLESS_INDEX;
    metadata.previous_morph_index = K_INVALID_BINDLESS_INDEX;
    metadata.current_material_wpo_index = K_INVALID_BINDLESS_INDEX;
    metadata.previous_material_wpo_index = K_INVALID_BINDLESS_INDEX;
    metadata.current_motion_vector_status_index = K_INVALID_BINDLESS_INDEX;
    metadata.previous_motion_vector_status_index = K_INVALID_BINDLESS_INDEX;
    metadata.publication_flags = 0u;
    metadata.pad0 = 0u;
    metadata.pad1 = 0u;
    metadata.pad2 = 0u;
    return metadata;
}

static inline bool HasVelocityDrawPublicationFlag(
    uint flags, uint bit_mask)
{
    return (flags & bit_mask) != 0u;
}

static inline bool HasMotionPublicationCapability(
    uint flags, uint bit_mask)
{
    return (flags & bit_mask) != 0u;
}

static inline bool LoadVelocityDrawMetadata(
    uint slot, uint draw_index, out VelocityDrawMetadata metadata)
{
    metadata = MakeInvalidVelocityDrawMetadata();
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return false;
    }

    StructuredBuffer<VelocityDrawMetadata> metadata_buffer
        = ResourceDescriptorHeap[slot];
    metadata = metadata_buffer[draw_index];
    return true;
}

static inline MaterialWpoPublication LoadMaterialWpoPublication(
    uint slot, uint publication_index)
{
    if (slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(slot)
        || publication_index == K_INVALID_BINDLESS_INDEX) {
        return MakeInvalidMaterialWpoPublication();
    }

    StructuredBuffer<MaterialWpoPublication> publications
        = ResourceDescriptorHeap[slot];
    return publications[publication_index];
}

static inline MotionVectorStatusPublication LoadMotionVectorStatusPublication(
    uint slot, uint publication_index)
{
    if (slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(slot)
        || publication_index == K_INVALID_BINDLESS_INDEX) {
        return MakeInvalidMotionVectorStatusPublication();
    }

    StructuredBuffer<MotionVectorStatusPublication> publications
        = ResourceDescriptorHeap[slot];
    return publications[publication_index];
}

static inline float3 ResolveMaterialWpoOffset(
    MaterialWpoPublication publication)
{
    const bool has_wpo = HasMotionPublicationCapability(
        publication.capability_flags,
        MOTION_PUBLICATION_CAPABILITY_USES_WORLD_POSITION_OFFSET);
    const bool has_payload = HasMotionPublicationCapability(
        publication.capability_flags,
        MOTION_PUBLICATION_CAPABILITY_HAS_RUNTIME_PAYLOAD);
    if (!has_wpo || !has_payload) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return publication.parameter_block0.xyz;
}

static inline float3 ResolveMotionVectorWorldOffset(
    MotionVectorStatusPublication publication)
{
    const bool has_mvwo = HasMotionPublicationCapability(
        publication.capability_flags,
        MOTION_PUBLICATION_CAPABILITY_USES_MOTION_VECTOR_WORLD_OFFSET);
    const bool has_payload = HasMotionPublicationCapability(
        publication.capability_flags,
        MOTION_PUBLICATION_CAPABILITY_HAS_RUNTIME_PAYLOAD);
    if (!has_mvwo || !has_payload) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return publication.parameter_block0.xyz;
}

static inline float3 ResolveCurrentMaterialWpoOffset(
    DrawFrameBindings draw_bindings, VelocityDrawMetadata velocity_metadata)
{
    if (!HasVelocityDrawPublicationFlag(
            velocity_metadata.publication_flags,
            VELOCITY_DRAW_PUBLICATION_FLAG_CURRENT_MATERIAL_WPO_VALID)) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    return ResolveMaterialWpoOffset(LoadMaterialWpoPublication(
        draw_bindings.current_material_wpo_slot,
        velocity_metadata.current_material_wpo_index));
}

static inline float3 ResolvePreviousMaterialWpoOffset(
    DrawFrameBindings draw_bindings, VelocityDrawMetadata velocity_metadata)
{
    if (!HasVelocityDrawPublicationFlag(
            velocity_metadata.publication_flags,
            VELOCITY_DRAW_PUBLICATION_FLAG_PREVIOUS_MATERIAL_WPO_VALID)) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    return ResolveMaterialWpoOffset(LoadMaterialWpoPublication(
        draw_bindings.previous_material_wpo_slot,
        velocity_metadata.previous_material_wpo_index));
}

static inline float3 ResolveCurrentMotionVectorWorldOffset(
    DrawFrameBindings draw_bindings, VelocityDrawMetadata velocity_metadata)
{
    if (!HasVelocityDrawPublicationFlag(
            velocity_metadata.publication_flags,
            VELOCITY_DRAW_PUBLICATION_FLAG_CURRENT_MOTION_VECTOR_STATUS_VALID)) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    return ResolveMotionVectorWorldOffset(LoadMotionVectorStatusPublication(
        draw_bindings.current_motion_vector_status_slot,
        velocity_metadata.current_motion_vector_status_index));
}

static inline float3 ResolvePreviousMotionVectorWorldOffset(
    DrawFrameBindings draw_bindings, VelocityDrawMetadata velocity_metadata)
{
    if (!HasVelocityDrawPublicationFlag(
            velocity_metadata.publication_flags,
            VELOCITY_DRAW_PUBLICATION_FLAG_PREVIOUS_MOTION_VECTOR_STATUS_VALID)) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    return ResolveMotionVectorWorldOffset(LoadMotionVectorStatusPublication(
        draw_bindings.previous_motion_vector_status_slot,
        velocity_metadata.previous_motion_vector_status_index));
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VELOCITYPUBLICATIONS_HLSLI

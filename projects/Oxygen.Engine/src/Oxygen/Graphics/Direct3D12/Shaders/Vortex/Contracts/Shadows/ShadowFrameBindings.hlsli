//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_CONTRACTS_SHADOWS_SHADOWFRAMEBINDINGS_HLSLI
#define OXYGEN_VORTEX_CONTRACTS_SHADOWS_SHADOWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Shadows/ShadowCascadeBinding.hlsli"
#include "Vortex/Contracts/Shadows/ProjectedLocalShadowRecord.hlsli"
#include "Vortex/Contracts/Shadows/CubeLocalShadowRecord.hlsli"
#include "Vortex/Contracts/Shadows/ShadowRecords.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"

struct VortexShadowFrameBindings {
    uint directional_records_srv;
    uint directional_record_count;
    uint projected_local_records_srv;
    uint projected_local_record_count;
    uint cube_local_records_srv;
    uint cube_local_record_count;
    uint cascade_records_srv;
    uint cascade_record_count;
    uint contact_depth_srv;
    uint view_status_srv;
    uint contact_enabled;
    uint sampling_flags;
    float2 contact_content_origin_px;
    float2 contact_content_extent_px;
    uint2 scene_generation;
    uint2 selection_revision;
    uint2 frame_sequence;
    uint2 view_generation;
    uint2 contact_texture_extent_px;
    uint2 reserved;
};

static inline VortexShadowFrameBindings MakeInvalidVortexShadowFrameBindings()
{
    VortexShadowFrameBindings bindings = (VortexShadowFrameBindings)0;
    bindings.directional_records_srv = K_INVALID_BINDLESS_INDEX;
    bindings.projected_local_records_srv = K_INVALID_BINDLESS_INDEX;
    bindings.cube_local_records_srv = K_INVALID_BINDLESS_INDEX;
    bindings.cascade_records_srv = K_INVALID_BINDLESS_INDEX;
    bindings.contact_depth_srv = K_INVALID_BINDLESS_INDEX;
    bindings.view_status_srv = K_INVALID_BINDLESS_INDEX;
    return bindings;
}

static inline VortexShadowFrameBindings LoadVortexShadowFrameBindings()
{
    const ViewFrameBindings view = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    if (!BX_IN_GLOBAL_SRV(view.shadow_frame_slot)) return MakeInvalidVortexShadowFrameBindings();
    StructuredBuffer<VortexShadowFrameBindings> buffer = ResourceDescriptorHeap[view.shadow_frame_slot];
    uint count, stride;
    buffer.GetDimensions(count, stride);
    if (count == 0u) return MakeInvalidVortexShadowFrameBindings();
    const VortexShadowFrameBindings bindings = buffer[0];
    const uint2 frame_identity = uint2((uint)frame_seq_num, (uint)(frame_seq_num >> 32u));
    if (any(bindings.frame_sequence != frame_identity)
        || any(bindings.view_generation != view.lighting_view_generation)) {
        return MakeInvalidVortexShadowFrameBindings();
    }
    if (view.lighting_frame_slot != K_INVALID_BINDLESS_INDEX) {
        const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
        if (!IsLightingPublicationReady(lighting)
            || bindings.view_status_srv != lighting.build_status_srv
            || any(bindings.scene_generation != lighting.scene_generation)
            || any(bindings.selection_revision != lighting.selection_revision)) {
            return MakeInvalidVortexShadowFrameBindings();
        }
    }
    return bindings;
}

static bool TryLoadDirectionalShadowFamily(VortexShadowFrameBindings bindings,
    uint selection_index, out DirectionalShadowRecord family)
{
    family = (DirectionalShadowRecord)0;
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    const LightShadowReference reference = LoadLightShadowReference(lighting.directional_shadow_map_srv, selection_index);
    if (reference.projection_kind != SHADOW_PROJECTION_CASCADED_2D
        || reference.selection_index != selection_index
        || reference.record_index >= bindings.directional_record_count
        || !BX_IN_GLOBAL_SRV(bindings.directional_records_srv)) return false;
    StructuredBuffer<DirectionalShadowRecord> records = ResourceDescriptorHeap[bindings.directional_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (reference.record_index >= count) return false;
    family = records[reference.record_index];
    return family.selection_index == selection_index && family.cascade_count != 0u
        && family.first_cascade <= bindings.cascade_record_count
        && family.cascade_count <= bindings.cascade_record_count - family.first_cascade;
}

static VortexShadowCascadeBinding LoadShadowCascade(VortexShadowFrameBindings bindings, uint index)
{
    VortexShadowCascadeBinding value = (VortexShadowCascadeBinding)0;
    value.surface_srv = K_INVALID_BINDLESS_INDEX;
    if (index >= bindings.cascade_record_count || !BX_IN_GLOBAL_SRV(bindings.cascade_records_srv)) return value;
    StructuredBuffer<VortexShadowCascadeBinding> records = ResourceDescriptorHeap[bindings.cascade_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (index < count) value = records[index];
    return value;
}

static ProjectedLocalShadowRecord LoadProjectedLocalShadow(VortexShadowFrameBindings bindings, uint index)
{
    ProjectedLocalShadowRecord value = (ProjectedLocalShadowRecord)0;
    value.surface_srv = K_INVALID_BINDLESS_INDEX;
    if (index >= bindings.projected_local_record_count || !BX_IN_GLOBAL_SRV(bindings.projected_local_records_srv)) return value;
    StructuredBuffer<ProjectedLocalShadowRecord> records = ResourceDescriptorHeap[bindings.projected_local_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (index < count) value = records[index];
    return value;
}

static CubeLocalShadowRecord LoadCubeLocalShadow(VortexShadowFrameBindings bindings, uint index)
{
    CubeLocalShadowRecord value = (CubeLocalShadowRecord)0;
    value.surface_srv = K_INVALID_BINDLESS_INDEX;
    if (index >= bindings.cube_local_record_count || !BX_IN_GLOBAL_SRV(bindings.cube_local_records_srv)) return value;
    StructuredBuffer<CubeLocalShadowRecord> records = ResourceDescriptorHeap[bindings.cube_local_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (index < count) value = records[index];
    return value;
}

#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_LIGHTING_HELPERS_HLSLI
#define OXYGEN_VORTEX_LIGHTING_HELPERS_HLSLI

#include "Vortex/Contracts/Lighting/LightingFrameBindings.hlsli"
#include "Vortex/Contracts/Shadows/ShadowRecords.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Services/Lighting/ClusterLookup.hlsli"

static inline LightingFrameBindings LoadResolvedLightingFrameBindings()
{
    const ViewFrameBindings view = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    const LightingFrameBindings lighting = LoadLightingFrameBindings(view.lighting_frame_slot);
    const uint2 frame_identity = uint2((uint)frame_seq_num, (uint)(frame_seq_num >> 32u));
    if (any(lighting.frame_sequence != frame_identity)
        || any(lighting.view_generation != view.lighting_view_generation)
        || ((lighting.directional_count != 0u || lighting.local_count != 0u)
            && all(lighting.scene_generation == 0u))) {
        return LoadLightingFrameBindings(K_INVALID_BINDLESS_INDEX);
    }
    return lighting;
}

static bool TryLoadDirectionalLight(LightingFrameBindings lighting, uint index,
    out DirectionalLightForwardData light)
{
    light = (DirectionalLightForwardData)0;
    if (!IsLightingPublicationReady(lighting) || index >= lighting.directional_count
        || !BX_IN_GLOBAL_SRV(lighting.directional_records_srv)) return false;
    StructuredBuffer<DirectionalLightForwardData> records = ResourceDescriptorHeap[lighting.directional_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (index >= count) return false;
    light = records[index];
    return light.selection_index == index;
}

static bool TryLoadLocalLight(LightingFrameBindings lighting, uint index,
    out ForwardLocalLightRecord light)
{
    light = (ForwardLocalLightRecord)0;
    if (!IsLightingPublicationReady(lighting) || index >= lighting.local_count
        || !BX_IN_GLOBAL_SRV(lighting.local_records_srv)) return false;
    StructuredBuffer<ForwardLocalLightRecord> records = ResourceDescriptorHeap[lighting.local_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (index >= count) return false;
    light = records[index];
    return light.selection_index == index;
}

static bool TryLoadAtmosphereDirectionalLight(uint atmosphere_slot,
    out DirectionalLightForwardData light)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    light = (DirectionalLightForwardData)0;
    for (uint index = 0u; index < lighting.directional_count; ++index) {
        if (TryLoadDirectionalLight(lighting, index, light)
            && light.atmosphere_light_slot == atmosphere_slot) return true;
    }
    light = (DirectionalLightForwardData)0;
    return false;
}

static float3 GetSunDirectionWS()
{
    DirectionalLightForwardData light;
    return TryLoadAtmosphereDirectionalLight(0u, light) ? light.direction_to_source_ws : 0.0f.xxx;
}

static bool HasSunLight()
{
    DirectionalLightForwardData light;
    return TryLoadAtmosphereDirectionalLight(0u, light);
}

static LightShadowReference LoadLightShadowReference(uint descriptor, uint index)
{
    LightShadowReference reference = (LightShadowReference)0;
    reference.record_index = K_INVALID_BINDLESS_INDEX;
    reference.selection_index = K_INVALID_BINDLESS_INDEX;
    if (!BX_IN_GLOBAL_SRV(descriptor)) return reference;
    StructuredBuffer<LightShadowReference> references = ResourceDescriptorHeap[descriptor];
    uint count, stride;
    references.GetDimensions(count, stride);
    if (index < count) reference = references[index];
    return reference;
}

static uint3 GetClusterDimensions()
{
    return LoadLightGridMetadata(LoadResolvedLightingFrameBindings().grid_metadata_srv).grid_size;
}
static uint GetClusterGridSlot()
{
    return LoadResolvedLightingFrameBindings().cluster_ranges_srv;
}
static uint GetClusterIndexListSlot()
{
    return LoadResolvedLightingFrameBindings().local_indices_srv;
}
static uint GetClusterIndex(float2 screen_pos, float linear_depth)
{
    return ComputeClusterIndex(screen_pos, linear_depth,
        LoadLightGridMetadata(LoadResolvedLightingFrameBindings().grid_metadata_srv));
}

#endif

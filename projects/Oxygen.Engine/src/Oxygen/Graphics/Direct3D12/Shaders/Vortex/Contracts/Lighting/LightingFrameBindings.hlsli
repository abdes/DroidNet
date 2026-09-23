//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_LIGHTING_FRAME_BINDINGS_HLSLI
#define OXYGEN_VORTEX_LIGHTING_FRAME_BINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Lighting/DirectionalLightForwardData.hlsli"
#include "Vortex/Contracts/Lighting/ForwardLocalLightRecord.hlsli"
#include "Vortex/Contracts/Lighting/LightGridData.hlsli"

static const uint LIGHTING_PUBLICATION_DISABLED = 0u;
static const uint LIGHTING_PUBLICATION_EMPTY = 1u;
static const uint LIGHTING_PUBLICATION_RECORDED = 2u;
static const uint LIGHTING_PUBLICATION_FAILED = 3u;

struct LightingFrameBindings
{
    uint directional_records_srv;
    uint local_records_srv;
    uint cluster_ranges_srv;
    uint local_indices_srv;
    uint directional_count;
    uint local_count;
    uint cluster_count;
    uint index_capacity;
    uint directional_shadow_map_srv;
    uint local_shadow_map_srv;
    uint build_status_srv;
    uint grid_metadata_srv;
    uint2 scene_generation;
    uint2 selection_revision;
    uint2 frame_sequence;
    uint2 view_generation;
    uint publication_state;
    uint brdf_energy_srv;
    uint brdf_model_revision;
    uint reserved;
};

static LightingFrameBindings LoadLightingFrameBindings(uint slot)
{
    LightingFrameBindings invalid_bindings = (LightingFrameBindings)0;
    invalid_bindings.directional_records_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.local_records_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.cluster_ranges_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.local_indices_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.directional_shadow_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.local_shadow_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.build_status_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.grid_metadata_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.brdf_energy_srv = K_INVALID_BINDLESS_INDEX;
    if (!BX_IN_GLOBAL_SRV(slot)) return invalid_bindings;
    StructuredBuffer<LightingFrameBindings> bindings = ResourceDescriptorHeap[slot];
    return bindings[0];
}

static bool IsLightingPublicationReady(LightingFrameBindings lighting)
{
    if ((lighting.publication_state != LIGHTING_PUBLICATION_RECORDED
        && lighting.publication_state != LIGHTING_PUBLICATION_EMPTY)
        || !BX_IN_GLOBAL_SRV(lighting.build_status_srv)) return false;
    StructuredBuffer<LightGridBuildStatus> statuses = ResourceDescriptorHeap[lighting.build_status_srv];
    LightGridBuildStatus status = statuses[0];
    return status.state == LIGHT_GRID_BUILD_VALID
        && all(status.selection_revision == lighting.selection_revision);
}

#endif

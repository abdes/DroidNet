//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_DEFERRED_LIGHT_CONSTANTS_HLSLI
#define OXYGEN_VORTEX_DEFERRED_LIGHT_CONSTANTS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

// One local light's inputs. A deferred-lighting pass publishes the records of
// all its local-light draws as one array; each draw passes the array as its
// inputs record and its own element as the draw index.
struct DeferredLightConstants
{
    float4x4 light_world_matrix;
    uint light_type;
    uint selection_index;
    uint light_geometry_vertices_srv;
    uint light_geometry_vertex_count;
};

static inline bool TryLoadDeferredLightConstants(
    uint records_slot, uint draw_index, out DeferredLightConstants constants)
{
    constants = (DeferredLightConstants)0;
    if (records_slot == K_INVALID_BINDLESS_INDEX) {
        return false;
    }
    StructuredBuffer<DeferredLightConstants> records
        = ResourceDescriptorHeap[records_slot];
    constants = records[draw_index];
    return true;
}

#endif

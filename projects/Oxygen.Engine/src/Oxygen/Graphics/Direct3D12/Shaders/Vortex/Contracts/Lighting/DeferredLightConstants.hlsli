//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_DEFERRED_LIGHT_CONSTANTS_HLSLI
#define OXYGEN_VORTEX_DEFERRED_LIGHT_CONSTANTS_HLSLI

struct DeferredLightConstants
{
    float4x4 light_world_matrix;
    uint light_type;
    uint selection_index;
    uint light_geometry_vertices_srv;
    uint light_geometry_vertex_count;
};

#endif

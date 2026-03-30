//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEREQUESTPROJECTION_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEREQUESTPROJECTION_HLSLI

#include "Renderer/Vsm/VsmProjectionData.hlsli"

// Request-generator routing record.
//
// Each element describes one active virtual shadow map the Stage 5 pass may
// request pages for. The record intentionally mixes:
// - projection data: how to transform a visible world-space point into the map
// - virtual layout metadata: how to convert that hit into a page-table slot
// - light-grid metadata: which clustered-light index to use for local-light
//   pruning

static const uint VSM_INVALID_LIGHT_INDEX = 0xffffffffu;
static const uint VSM_INVALID_CUBE_FACE_INDEX = 0xffffffffu;

struct VsmPageRequestProjection
{
    VsmProjectionData projection;
    uint map_id;
    uint first_page_table_entry;
    uint map_pages_x;
    uint map_pages_y;
    uint pages_x;
    uint pages_y;
    uint page_offset_x;
    uint page_offset_y;
    uint level_count;
    uint coarse_level;
    uint light_index;
    uint cube_face_index;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEREQUESTPROJECTION_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPROJECTIONDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPROJECTIONDATA_HLSLI

// Shader-facing per-virtual-map projection payload.
//
// This is intentionally separate from the richer CPU cache snapshots. The
// projection/composite path, invalidation path, and shadow-view creation path
// need a compact GPU upload record with only the data required to interpret one
// virtual shadow map in light/view space.
//
// The upload/publication plumbing is still future work, but this ABI is already
// fixed so later passes can build against it without redefining the layout.

static const uint VSM_PROJECTION_LIGHT_TYPE_LOCAL = 0u;
static const uint VSM_PROJECTION_LIGHT_TYPE_DIRECTIONAL = 1u;

struct VsmProjectionData
{
    // World -> light/view transform for this virtual map.
    float4x4 view_matrix;
    // Light/view -> clip transform for this virtual map.
    float4x4 projection_matrix;
    // Light/view origin in world space. W is padding for 16-byte alignment.
    float4 view_origin_ws_pad;
    // Page-space clipmap pan offset. Meaningful for directional clipmaps only.
    int2 clipmap_corner_offset;
    // Directional clipmap level. Ignored for local lights.
    uint clipmap_level;
    // One of VSM_PROJECTION_LIGHT_TYPE_*.
    uint light_type;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPROJECTIONDATA_HLSLI

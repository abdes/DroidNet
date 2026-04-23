//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWRECEIVERMASK_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWRECEIVERMASK_HLSLI

static const uint CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_VALID = 1u << 0u;
static const uint CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_EMPTY = 1u << 1u;
static const uint CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_HIERARCHY_BUILT
    = 1u << 2u;

struct ConventionalShadowReceiverMaskSummary
{
    float4 full_rect_center_half_extent;
    float4 raw_xy_min_max;
    float4 raw_depth_and_dilation;
    uint target_array_slice;
    uint flags;
    uint sample_count;
    uint occupied_tile_count;
    uint hierarchy_occupied_tile_count;
    uint base_tile_resolution;
    uint hierarchy_tile_resolution;
    uint dilation_tile_radius;
    uint hierarchy_reduction;
    uint3 _pad0;
};

#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWRECEIVERANALYSIS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWRECEIVERANALYSIS_HLSLI

static const uint CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_VALID = 1u << 0u;
static const uint CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_EMPTY = 1u << 1u;
static const uint CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_FALLBACK_TO_LEGACY_RECT
    = 1u << 2u;

struct ConventionalShadowReceiverAnalysis
{
    float4 raw_xy_min_max;
    float4 raw_depth_and_dilation;
    float4 full_rect_center_half_extent;
    float4 legacy_rect_center_half_extent;
    float4 full_depth_and_area_ratios;
    float full_depth_ratio;
    uint sample_count;
    uint target_array_slice;
    uint flags;
};

#endif

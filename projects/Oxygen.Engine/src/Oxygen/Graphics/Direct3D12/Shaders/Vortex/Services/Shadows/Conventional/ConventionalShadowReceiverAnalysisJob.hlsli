//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWRECEIVERANALYSISJOB_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWRECEIVERANALYSISJOB_HLSLI

struct ConventionalShadowReceiverAnalysisJob
{
    float4x4 light_rotation_matrix;
    float4 full_rect_center_half_extent;
    float4 legacy_rect_center_half_extent;
    float4 split_and_full_depth_range;
    float4 shading_margins;
    uint target_array_slice;
    uint flags;
    uint _pad0;
    uint _pad1;
};

#endif

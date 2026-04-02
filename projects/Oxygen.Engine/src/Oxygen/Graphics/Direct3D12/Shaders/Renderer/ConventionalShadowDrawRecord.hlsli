//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWDRAWRECORD_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWDRAWRECORD_HLSLI

struct ConventionalShadowDrawRecord
{
    float4 world_bounding_sphere;
    uint draw_index;
    uint partition_index;
    uint partition_pass_mask;
    uint primitive_flags;
};

#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWCASTERCULLINGPARTITION_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWCASTERCULLINGPARTITION_HLSLI

struct ConventionalShadowCasterCullingPartition
{
    uint record_begin;
    uint record_count;
    uint command_uav_index;
    uint count_uav_index;
    uint max_commands_per_job;
    uint partition_index;
    uint pass_mask;
    uint _pad0;
};

#endif

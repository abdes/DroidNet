//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWINDIRECTDRAWCOMMAND_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_CONVENTIONALSHADOWINDIRECTDRAWCOMMAND_HLSLI

struct ConventionalShadowIndirectDrawCommand
{
    uint draw_index;
    uint vertex_count_per_instance;
    uint instance_count;
    uint start_vertex_location;
    uint start_instance_location;
};

#endif

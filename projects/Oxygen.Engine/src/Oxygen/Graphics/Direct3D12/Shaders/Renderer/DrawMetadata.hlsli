//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWMETADATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWMETADATA_HLSLI

// ABI: must match sizeof(oxygen::engine::DrawMetadata) == 64
struct DrawMetadata
{
    uint vertex_buffer_index;
    uint index_buffer_index;
    uint first_index;
    int base_vertex;
    uint is_indexed;
    uint instance_count;
    uint index_count;
    uint vertex_count;
    uint material_handle;
    uint transform_index;
    uint instance_metadata_buffer_index;
    uint instance_metadata_offset;
    uint flags;
    uint padding[3];
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_DRAWMETADATA_HLSLI

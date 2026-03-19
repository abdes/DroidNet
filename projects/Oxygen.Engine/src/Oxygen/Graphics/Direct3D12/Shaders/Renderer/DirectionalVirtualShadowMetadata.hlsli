//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALVIRTUALSHADOWMETADATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALVIRTUALSHADOWMETADATA_HLSLI

#ifndef OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS
#define OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS 12
#endif

struct DirectionalVirtualClipMetadata
{
    float4 origin_page_scale; // xy origin_ls, z page_world_size, w depth_scale
    float4 bias_reserved;     // x depth_bias, yzw reserved
    int4 clipmap_level_data;  // x clipmap level, y remaining levels, zw reserved
};

struct DirectionalVirtualShadowMetadata
{
    uint shadow_instance_index;
    uint flags;
    float constant_bias;
    float normal_bias;

    uint clip_level_count;
    uint pages_per_axis;
    uint page_size_texels;
    uint page_table_offset;
    uint coarse_clip_mask;
    float receiver_normal_bias_scale;
    float receiver_constant_bias_scale;
    float receiver_slope_bias_scale;
    float raster_constant_bias_scale;
    float raster_slope_bias_scale;
    uint reserved0;
    uint reserved1;
    float4 clipmap_selection_world_origin_lod_bias; // xyz clipmap selection origin ws, w selection lod bias
    float4 clipmap_receiver_origin_lod_bias; // xyz receiver-bias origin ws, w receiver clip bias
    int4 clip_grid_origin_x_packed[3];
    int4 clip_grid_origin_y_packed[3];

    DirectionalVirtualClipMetadata clip_metadata[OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS];
    float4x4 light_view;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALVIRTUALSHADOWMETADATA_HLSLI

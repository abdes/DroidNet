//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

struct DirectionalLightForwardData
{
    float3 direction;
    float source_radius;

    float3 color;
    float illuminance_lux;

    float specular_scale;
    float diffuse_scale;
    uint shadow_flags;
    uint light_function_atlas_index;

    uint cascade_count;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

struct LightingFrameBindings
{
    uint local_light_buffer_srv;
    uint light_view_data_srv;
    uint grid_metadata_buffer_srv;
    uint grid_indirection_srv;
    uint directional_light_indices_srv;

    int3 grid_size;
    float reserved_grid0;

    float3 grid_z_params;
    float reserved_grid1;

    uint num_grid_cells;
    uint max_culled_lights_per_cell;
    uint directional_light_count;
    uint local_light_count;

    uint has_directional_light;
    uint affects_translucent_lighting;
    uint flags;
    uint reserved_flags;

    float4 pre_view_translation_offset;
    DirectionalLightForwardData directional;

    uint directional_lights_slot;
    uint positional_lights_slot;
};

static LightingFrameBindings LoadLightingFrameBindings(uint slot)
{
    LightingFrameBindings invalid_bindings = (LightingFrameBindings)0;
    invalid_bindings.local_light_buffer_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.light_view_data_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.grid_metadata_buffer_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.grid_indirection_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.directional_light_indices_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.directional_lights_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.positional_lights_slot = K_INVALID_BINDLESS_INDEX;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<LightingFrameBindings> bindings_buffer = ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGFRAMEBINDINGS_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

struct DrawFrameBindings
{
    uint draw_metadata_slot;
    uint current_worlds_slot;
    uint previous_worlds_slot;
    uint normal_matrices_slot;
    uint material_shading_constants_slot;
    uint procedural_grid_material_constants_slot;
    uint instance_data_slot;
    uint current_skinned_pose_slot;
    uint previous_skinned_pose_slot;
    uint current_morph_slot;
    uint previous_morph_slot;
    uint current_material_wpo_slot;
    uint previous_material_wpo_slot;
    uint current_motion_vector_status_slot;
    uint previous_motion_vector_status_slot;
    uint velocity_draw_metadata_slot;
};

static DrawFrameBindings LoadDrawFrameBindings(uint slot)
{
    DrawFrameBindings invalid_bindings;
    invalid_bindings.draw_metadata_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.current_worlds_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.previous_worlds_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.normal_matrices_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.material_shading_constants_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.procedural_grid_material_constants_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.instance_data_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.current_skinned_pose_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.previous_skinned_pose_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.current_morph_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.previous_morph_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.current_material_wpo_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.previous_material_wpo_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.current_motion_vector_status_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.previous_motion_vector_status_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.velocity_draw_metadata_slot = K_INVALID_BINDLESS_INDEX;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<DrawFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI

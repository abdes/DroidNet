//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Reports what a camera pass sees through the b1 record it was recorded with:
// the view-frame bindings slot, the scene-texture record it routes to and the
// SceneColor SRV that record publishes. Not part of the runtime archive.
#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct ViewRecordProbeInputs
{
    uint output_uav;
    uint record_index;
    uint2 reserved;
};

[numthreads(1, 1, 1)]
void CS()
{
    StructuredBuffer<ViewRecordProbeInputs> inputs_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const ViewRecordProbeInputs inputs = inputs_buffer[0];
    RWByteAddressBuffer output = ResourceDescriptorHeap[inputs.output_uav];

    const ViewFrameBindings view = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    const SceneTextureBindingData scene
        = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    output.Store4(inputs.record_index * 16u, uint4(bindless_view_frame_bindings_slot,
        view.scene_texture_frame_slot, scene.scene_color_srv, frame_slot));
}

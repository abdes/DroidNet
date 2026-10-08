//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Selection outline mask: rasterizes the outlined draws with the depth
// prepass vertex shader. R holds the outline level (0.5 outlined, 1.0 active)
// and G whether the surface is the nearest one at that pixel. Both channels
// are combined with MAX blending.

#include "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl"

struct SelectionOutlineMaskConstants
{
    uint scene_depth_srv;
    float level;
    uint reverse_z;
    uint test_occlusion;
};

[shader("pixel")]
float2 VortexSelectionOutlineMaskPS(DepthPrepassVSOutput input) : SV_Target0
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif

    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return float2(0.0f, 0.0f);
    }
    StructuredBuffer<SelectionOutlineMaskConstants> constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SelectionOutlineMaskConstants pc = constants[0];

    float visible = 1.0f;
    if (pc.test_occlusion != 0u && BX_IsValidSlot(pc.scene_depth_srv)) {
        Texture2D<float> scene_depth = ResourceDescriptorHeap[pc.scene_depth_srv];
        const float scene_z
            = scene_depth.Load(int3(int2(input.position.xy), 0));
        // The scene depth came from a different shader: accept a small
        // relative difference as the same surface.
        const float tolerance = 1.0e-5f + abs(scene_z) * 2.0e-3f;
        visible = pc.reverse_z != 0u
            ? (input.position.z >= scene_z - tolerance ? 1.0f : 0.0f)
            : (input.position.z <= scene_z + tolerance ? 1.0f : 0.0f);
    }
    return float2(pc.level, visible);
}

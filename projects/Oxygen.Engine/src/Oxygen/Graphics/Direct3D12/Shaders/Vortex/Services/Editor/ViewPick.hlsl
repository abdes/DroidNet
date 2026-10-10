//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// View pick pass: writes the draw index (plus one, zero meaning no hit) and
// the raw device depth of the nearest pickable surface for each pixel of the
// pick rectangle.

#include "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl"

[shader("pixel")]
uint2 VortexViewPickPS(DepthPrepassVSOutput input) : SV_Target0
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, input.draw_index, linear_sampler));
#endif
    return uint2(input.draw_index + 1u, asuint(input.position.z));
}

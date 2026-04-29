//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Stages/BasePass/BasePassGBuffer.hlsl"
#include "Vortex/Contracts/View/ViewColorHelpers.hlsli"

struct WireframePassConstants
{
    float4 wire_color;
    float apply_exposure_compensation;
    float3 padding;
};

static inline float4 LoadWireframeColor()
{
    float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float apply_exposure_compensation = 0.0f;
    if (BX_IsValidSlot(g_PassConstantsIndex))
    {
        ConstantBuffer<WireframePassConstants> pc =
            ResourceDescriptorHeap[g_PassConstantsIndex];
        color = pc.wire_color;
        apply_exposure_compensation = pc.apply_exposure_compensation;
    }

    if (apply_exposure_compensation > 0.5f)
    {
        color.rgb /= max(GetExposure(), 1.0e-6f);
    }

    return color;
}

[shader("pixel")]
float4 BasePassWireframePS(BasePassGBufferVSOutput input) : SV_Target0
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif

    return LoadWireframeColor();
}

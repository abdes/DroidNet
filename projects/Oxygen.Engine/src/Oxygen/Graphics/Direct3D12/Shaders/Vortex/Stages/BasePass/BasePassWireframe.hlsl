//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Stages/BasePass/BasePassGBuffer.hlsl"
#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"

struct WireframePassConstants
{
    float4 wire_color;
    float write_pre_exposed;
    float3 padding;
};

static inline float4 LoadWireframeColor()
{
    float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float write_pre_exposed = 0.0f;
    if (BX_IsValidSlot(g_PassConstantsIndex))
    {
        ConstantBuffer<WireframePassConstants> pc =
            ResourceDescriptorHeap[g_PassConstantsIndex];
        color = pc.wire_color;
        write_pre_exposed = pc.write_pre_exposed;
    }

    if (write_pre_exposed > 0.5f)
    {
        color.rgb *= GetPreExposure();
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

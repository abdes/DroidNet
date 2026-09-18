//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Native arithmetic qualification; this shader is not part of the runtime archive.
#include "Vortex/Services/PostProcess/ToneMappingBounds.hlsli"
#include "Vortex/Services/PostProcess/ToneMappingFastBounds.hlsli"
#include "Vortex/Contracts/View/HdrHardwareSampling.hlsli"
#include "Vortex/Services/PostProcess/HdrSceneComposition.hlsli"

cbuffer RootConstants : register(b2, space0) {
    uint unused;
    uint pass_index;
}

struct ProbeConstants { uint inputs; uint output; uint count; uint reserved; };

[numthreads(64, 1, 1)]
void CS(uint3 thread : SV_DispatchThreadID)
{
    StructuredBuffer<ProbeConstants> constants = ResourceDescriptorHeap[pass_index];
    const ProbeConstants pass = constants[0];
    if (thread.x >= pass.count) return;
    StructuredBuffer<uint4> input = ResourceDescriptorHeap[pass.inputs];
    RWByteAddressBuffer output = ResourceDescriptorHeap[pass.output];
    if (pass.reserved == 64u) {
        const float4 bounds = asfloat(input[thread.x * 2u]);
        const float4 settings = asfloat(input[thread.x * 2u + 1u]);
        const float2 attenuation = SceneBoundAttenuate(bounds.xy, bounds.zw, settings.x);
        output.Store4(thread.x * 32u, asuint(float4(attenuation,
            SceneArithmeticBound(attenuation, uint(settings.y), settings.z))));
        output.Store4(thread.x * 32u + 16u, asuint(float4(
            SceneTransmittanceArithmetic(bounds.zw), SceneBoundAtMaximum(bounds.xy, settings.x))));
        return;
    }
    if (pass.reserved == 32u) {
        const float4 value = asfloat(input[thread.x * 3u]);
        const float4 gradient = asfloat(input[thread.x * 3u + 1u]);
        const uint3 extent = uint3(asfloat(input[thread.x * 3u + 2u]).xyz);
        const float2 interval = HdrHardwareFilterInterval(value.x, value.yz, gradient.xyz, extent, value.w != 0.0);
        output.Store4(thread.x * 32u, asuint(float4(interval, HdrFilterArithmetic(3u, value.w != 0.0))));
        output.Store4(thread.x * 32u + 16u, 0u.xxxx);
        return;
    }
    if (pass.reserved != 0u) {
        ToneRgbBounds bounds;
        const uint mapper = (pass.reserved >> 1u) & 3u;
        const float gamma = (pass.reserved & 16u) != 0u ? .8 : 2.2;
        const bool valid = ToneDisplayQuickBounds(asfloat(input[thread.x * 2u]),
            asfloat(input[thread.x * 2u + 1u]), 1.0, mapper, gamma, true, .25.xxx,
            uint2(1, 0), bounds);
        output.Store4(thread.x * 32u, asuint(float4(bounds.low, valid ? 1.0 : 0.0)));
        output.Store4(thread.x * 32u + 16u, asuint(float4(bounds.high, 0.0)));
        return;
    }
    const float4 value = asfloat(input[thread.x]);
    output.Store4(thread.x * 32u, asuint(float4(ToneDown(value.x), ToneUp(value.x),
        ToneAdd(value.xy, value.zw))));
    output.Store4(thread.x * 32u + 16u, asuint(float4(ToneMultiply(value.xy, value.zw),
        TonePower(value.xy, 2.0.xx))));
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Runs production material evaluation on constants a test wrote. Each thread
// group is one 2x2 quad, which gives texture sampling its derivatives: a thread
// reads `uvs` plus `uv_gradient` times its position in the quad.

#include "Vortex/Materials/ForwardMaterialEval.hlsli"

struct MaterialSurfaceProbeInput {
    MaterialShadingConstants material;
    float4 uvs;
    float2 uv_gradient;
    uint2 reserved;
};

struct ProbeArguments {
    uint4 decode;
    uint4 reserved;
};

cbuffer ProbeRoot : register(b2, space0) {
    uint g_RecordKind;
    uint g_ProbeArguments;
};

// Arguments: input SRV, output UAV, decoded words per record, first element.
[numthreads(2, 2, 1)]
void CS(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    StructuredBuffer<ProbeArguments> arguments = ResourceDescriptorHeap[g_ProbeArguments];
    const uint4 args = arguments[0].decode;
    StructuredBuffer<MaterialSurfaceProbeInput> inputs = ResourceDescriptorHeap[args.x];
    const MaterialSurfaceProbeInput input = inputs[args.w + group.x];

    const float2 step = input.uv_gradient * float2(thread.xy);
    const float4 uvs = input.uvs + float4(step, step);

    MaterialSurface surface = (MaterialSurface)0;
    surface.N = float3(0.0, 0.0, 1.0);
    ApplyMaterialConstants(surface, input.material, float3(0.0, 0.0, 1.0),
        float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), uvs);

    if (thread.x == 0u && thread.y == 0u) {
        RWByteAddressBuffer output = ResourceDescriptorHeap[args.y];
        const uint address = group.x * args.z * 4u;
        output.Store4(address, asuint(float4(surface.base_rgb, surface.base_a)));
        output.Store4(address + 16u,
            asuint(float4(surface.metalness, surface.roughness, surface.ao, 0.0)));
        output.Store4(address + 32u, asuint(float4(surface.emissive, 0.0)));
        output.Store4(address + 48u, asuint(float4(surface.N, 0.0)));
    }
}

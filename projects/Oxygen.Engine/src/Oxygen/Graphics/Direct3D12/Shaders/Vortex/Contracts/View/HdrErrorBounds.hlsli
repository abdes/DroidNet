//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_ERROR_BOUNDS_HLSLI
#define VORTEX_HDR_ERROR_BOUNDS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/HdrIntervalMath.hlsli"

static uint HdrProducerBoundOffset(uint product)
{
    return product == 5u ? 80u : product == 6u ? 96u : 112u;
}

static void RecordHdrStoreBounds(float4 value, float4 reference_low, float4 reference_high,
    float inverse_p, uint product, uint status_uav, uint fp16_store)
{
    if (status_uav == K_INVALID_BINDLESS_INDEX) return;
    const float4 stored = fp16_store != 0u ? HdrRoundToHalf(value) : value;
    float2 rgb = 0.0.xx;
    [unroll] for (uint c = 0u; c < 3u; ++c) {
        const float2 component = HdrEnclosureBound(stored[c], float2(reference_low[c], reference_high[c]));
        rgb = max(rgb, component);
    }
    rgb.y = HdrUpperProduct(rgb.y, inverse_p);
    const float2 transmission = HdrEnclosureBound(stored.a, float2(reference_low.a, reference_high.a));
    RWByteAddressBuffer status = ResourceDescriptorHeap[status_uav];
    const uint offset = HdrProducerBoundOffset(product);
    uint unused;
    status.InterlockedMax(offset, asuint(rgb.x), unused);
    status.InterlockedMax(offset + 4u, asuint(rgb.y), unused);
    status.InterlockedMax(offset + 8u, asuint(transmission.x), unused);
    status.InterlockedMax(offset + 12u, asuint(transmission.y), unused);
}

#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_ERROR_BOUNDS_HLSLI
#define VORTEX_HDR_ERROR_BOUNDS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

static float HdrBoundInfinity() { return asfloat(0x7f800000u); }

// Preserve exact zero. Positive subnormal bounds round outward to the smallest
// normal value so later FP32 arithmetic cannot silently flush the bound away.
static float HdrBoundUp(float value)
{
    const uint bits = asuint(value);
    if (bits == 0u) return 0.0;
    if (!isfinite(value) || (bits & 0x80000000u) != 0u) return HdrBoundInfinity();
    return asfloat(min(max(bits, 0x00800000u) + 4u, 0x7f800000u));
}

static float HdrBoundDown(float value)
{
    if (value <= 0.0 || !isfinite(value)) return 0.0;
    const uint bits = asuint(value);
    return bits <= 0x00800004u ? 0.0 : asfloat(bits - 4u);
}

static float HdrUpperSum(float a, float b)
{
    const float result = a + b;
    return result == 0.0 && (asuint(a) != 0u || asuint(b) != 0u)
        ? asfloat(0x00800000u) : HdrBoundUp(result);
}

static float HdrUpperProduct(float a, float b)
{
    if (asuint(a) == 0u || asuint(b) == 0u) return 0.0;
    const float result = a * b;
    return result == 0.0 ? asfloat(0x00800000u) : HdrBoundUp(result);
}

static float2 HdrReferenceInterval(float observed, float2 bound)
{
    if (!isfinite(observed) || observed < 0.0 || !all(isfinite(bound))
        || any(bound < 0.0) || bound.x >= 1.0)
        return float2(0.0, HdrBoundInfinity());
    if (all(bound == 0.0.xx)) return observed.xx;
    const float low = HdrBoundDown(HdrBoundDown(max(observed - bound.y, 0.0))
        / HdrUpperSum(1.0, bound.x));
    const float numerator = HdrUpperSum(observed, bound.y);
    const float high = numerator == 0.0 ? 0.0
        : HdrBoundUp(max(numerator / HdrBoundDown(1.0 - bound.x), asfloat(0x00800000u)));
    return float2(low, high);
}

// Encode an enclosure using a bounded relative coefficient plus an absolute
// remainder. Tiny values can therefore use the absolute allowance instead of
// assigning a unit relative error to the entire product.
static float2 HdrEnclosureBound(float observed, float2 reference)
{
    if (!isfinite(observed) || observed < 0.0 || !all(isfinite(reference))
        || reference.x < 0.0 || reference.y < reference.x)
        return float2(0.0, HdrBoundInfinity());
    if (all(asuint(reference) == asuint(observed).xx)) return 0.0.xx;
    float error = HdrBoundUp(max(abs(observed - reference.x), abs(observed - reference.y)));
    if (error == 0.0) error = asfloat(0x00800000u);
    const float relative = reference.x > 0.0
        ? min(HdrBoundDown(error / reference.x), 1.0 / 2048.0) : 0.0;
    const float covered = HdrBoundDown(relative * reference.x);
    const float absolute = HdrBoundUp(max(error - covered, 0.0));
    return float2(relative, absolute);
}

static uint HdrProducerBoundOffset(uint product)
{
    return product == 5u ? 80u : product == 6u ? 96u : 112u;
}

static void RecordHdrStoreBounds(float4 value, float4 reference_low, float4 reference_high,
    float inverse_p, uint product, uint status_uav, uint fp16_store)
{
    if (status_uav == K_INVALID_BINDLESS_INDEX) return;
    const float4 stored = fp16_store != 0u ? f16tof32(f32tof16(value)) : value;
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

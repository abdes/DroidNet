//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_INTERVAL_MATH_HLSLI
#define VORTEX_HDR_INTERVAL_MATH_HLSLI

// D3D format conversion and f32tof16 truncate. Explicit nearest-even rounding
// is required by the frozen 2^-25 half-subnormal store budget. Once decoded,
// this value is exactly representable and survives a typed half store intact.
static float HdrRoundToHalf(float value)
{
    const uint packed = f32tof16(value);
    uint magnitude = packed & 0x7fffu;
    if (magnitude < 0x7bffu) {
        const float low = f16tof32(magnitude);
        const float high = f16tof32(magnitude + 1u);
        const float midpoint = (low + high) * 0.5;
        const uint source_magnitude = asuint(value) & 0x7fffffffu;
        const uint midpoint_bits = asuint(midpoint);
        if (source_magnitude > midpoint_bits
            || (source_magnitude == midpoint_bits && (magnitude & 1u) != 0u))
            ++magnitude;
    }
    return f16tof32((packed & 0x8000u) | magnitude);
}

static float2 HdrRoundToHalf(float2 value)
{
    return float2(HdrRoundToHalf(value.x), HdrRoundToHalf(value.y));
}

static float3 HdrRoundToHalf(float3 value)
{
    return float3(HdrRoundToHalf(value.x), HdrRoundToHalf(value.y), HdrRoundToHalf(value.z));
}

static float4 HdrRoundToHalf(float4 value)
{
    return float4(HdrRoundToHalf(value.rgb), HdrRoundToHalf(value.a));
}

static float HdrBoundInfinity() { return asfloat(0x7f800000u); }

// Bit validation rejects negative subnormal metadata even on FTZ hardware.
// Negative zero is mathematically zero and remains acceptable.
static bool HdrFiniteNonnegative(float value)
{
    const uint bits = asuint(value);
    return bits <= 0x7f7fffffu || bits == 0x80000000u;
}

// Preserve exact zero. Positive subnormal bounds round outward to the smallest
// normal value so later FP32 arithmetic cannot silently flush the bound away.
static float HdrBoundUp(float value)
{
    const uint bits = asuint(value);
    if ((bits & 0x7fffffffu) == 0u) return 0.0;
    if (!isfinite(value) || (bits & 0x80000000u) != 0u) return HdrBoundInfinity();
    return asfloat(min(max(bits, 0x00800000u) + 4u, 0x7f800000u));
}

static float HdrBoundDown(float value)
{
    if (value <= 0.0 || !isfinite(value)) return 0.0;
    const uint bits = asuint(value);
    return bits <= 0x00800004u ? 0.0 : asfloat(bits - 4u);
}

// Widen operands before arithmetic: widening only a flushed result can miss
// a subnormal multiplied by a large gain, or added to a nearby normal value.
static float HdrUpperOperand(float value)
{
    const uint bits = asuint(value);
    if ((bits & 0x7fffffffu) == 0u) return 0.0;
    if (!HdrFiniteNonnegative(value)) return HdrBoundInfinity();
    return asfloat(max(bits, 0x00800000u));
}

static float HdrUpperSum(float a, float b)
{
    const float result = HdrUpperOperand(a) + HdrUpperOperand(b);
    return result == 0.0 && ((asuint(a) & 0x7fffffffu) != 0u || (asuint(b) & 0x7fffffffu) != 0u)
        ? asfloat(0x00800000u) : HdrBoundUp(result);
}

static float HdrUpperProduct(float a, float b)
{
    if ((asuint(a) & 0x7fffffffu) == 0u || (asuint(b) & 0x7fffffffu) == 0u) return 0.0;
    const float result = HdrUpperOperand(a) * HdrUpperOperand(b);
    return result == 0.0 ? asfloat(0x00800000u) : HdrBoundUp(result);
}

// Nonnegative finite endpoints; compare bits so FTZ cannot erase a tiny gap.
static float HdrUpperDifference(float high, float low)
{
    const uint high_bits = asuint(high) & 0x7fffffffu;
    const uint low_bits = asuint(low) & 0x7fffffffu;
    if (high_bits <= low_bits) return 0.0;
    const float difference = high - low;
    return difference == 0.0 ? asfloat(0x00800000u) : HdrBoundUp(difference);
}

static float2 HdrReferenceInterval(float observed, float2 bound)
{
    if (!HdrFiniteNonnegative(observed) || !HdrFiniteNonnegative(bound.x)
        || !HdrFiniteNonnegative(bound.y) || bound.x >= 1.0)
        return float2(0.0, HdrBoundInfinity());
    if (all((asuint(bound) & 0x7fffffffu) == 0u.xx)) return observed.xx;
    const float lower_observed = (asuint(observed) & 0x7fffffffu) < 0x00800000u ? 0.0 : observed;
    const float low = HdrBoundDown(HdrBoundDown(max(lower_observed - HdrUpperOperand(bound.y), 0.0))
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

#endif

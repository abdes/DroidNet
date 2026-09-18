//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_TONE_MAPPING_BOUNDS_HLSLI
#define VORTEX_TONE_MAPPING_BOUNDS_HLSLI

#include "Vortex/Services/PostProcess/ToneMapping.hlsli"

// Signed outward operations. Eight representable steps cover the evaluator
// and consumer's allowed FP32 rounding; subnormal operands include FTZ.
static float ToneDown(float value)
{
    const uint bits = asuint(value);
    const uint magnitude = bits & 0x7fffffffu;
    if (magnitude > 0x7f800000u) return asfloat(0xff800000u);
    if (magnitude == 0x7f800000u)
        return (bits & 0x80000000u) != 0u ? value : asfloat(0x7f7fffffu);
    if (magnitude < 0x00800000u) return asfloat(0x80800000u);
    if ((bits & 0x80000000u) != 0u)
        return asfloat(min(bits + 8u, 0xff800000u));
    return magnitude <= 0x00800008u ? 0.0 : asfloat(bits - 8u);
}

static float ToneUp(float value)
{
    return asfloat(asuint(ToneDown(asfloat(asuint(value) ^ 0x80000000u))) ^ 0x80000000u);
}

static float2 ToneOperands(float2 value)
{
    if ((asuint(value.x) & 0x7fffffffu) < 0x00800000u
        && (asuint(value.x) & 0x7fffffffu) != 0u)
        value.x = (asuint(value.x) & 0x80000000u) != 0u ? asfloat(0x80800000u) : 0.0;
    if ((asuint(value.y) & 0x7fffffffu) < 0x00800000u
        && (asuint(value.y) & 0x7fffffffu) != 0u)
        value.y = (asuint(value.y) & 0x80000000u) != 0u ? 0.0 : asfloat(0x00800000u);
    return value;
}

static bool ToneExactZero(float2 value)
{
    return all((asuint(value) & 0x7fffffffu.xx) == 0u.xx);
}

static float2 ToneNonnegative(float2 value)
{
    return float2((asuint(value.x) & 0x80000000u) != 0u ? 0.0 : value.x,
        (asuint(value.y) & 0x80000000u) != 0u ? 0.0 : value.y);
}

static float2 ToneSaturate(float2 value)
{
    value = ToneNonnegative(value);
    return asfloat(min(asuint(value), asuint(1.0).xx));
}

static float2 ToneAdd(float2 a, float2 b)
{
    if (ToneExactZero(a) && ToneExactZero(b)) return 0.0.xx;
    a = ToneOperands(a); b = ToneOperands(b);
    return float2(ToneDown(a.x + b.x), ToneUp(a.y + b.y));
}

static float2 ToneSubtract(float2 a, float2 b)
{
    return ToneAdd(a, -b.yx);
}

static float2 ToneMultiply(float2 a, float2 b)
{
    if ((ToneExactZero(a) && all(isfinite(b)))
        || (ToneExactZero(b) && all(isfinite(a)))) return 0.0.xx;
    a = ToneOperands(a); b = ToneOperands(b);
    const float4 values = float4(a.x * b.x, a.x * b.y, a.y * b.x, a.y * b.y);
    if (any(isnan(values))) return float2(asfloat(0xff800000u), asfloat(0x7f800000u));
    return float2(ToneDown(min(min(values.x, values.y), min(values.z, values.w))),
        ToneUp(max(max(values.x, values.y), max(values.z, values.w))));
}

static float2 ToneReciprocal(float2 a)
{
    a = ToneOperands(a);
    if (a.x <= 0.0 && a.y >= 0.0)
        return float2(asfloat(0xff800000u), asfloat(0x7f800000u));
    // D3D reciprocal error <=2^-21. Four times that tolerance covers
    // both endpoint evaluation and the consumer, including outward rounding.
    const float2 values = float2(rcp(a.y), rcp(a.x));
    return float2(ToneDown(values.x - abs(values.x) * 1.9073486328125e-6),
        ToneUp(values.y + abs(values.y) * 1.9073486328125e-6));
}

static float2 ToneDivide(float2 a, float2 b)
{
    return ToneMultiply(a, ToneReciprocal(b));
}

static float2 TonePower(float2 base, float2 exponent)
{
    base = ToneOperands(ToneNonnegative(base));
    if (exponent.x <= 0.0 || !all(isfinite(exponent)))
        return float2(0.0, asfloat(0x7f800000u));
    if ((asuint(base.y) & 0x7fffffffu) == 0u) return 0.0.xx;
    const float2 logs = log2(base);
    // log2 error is absolute 2^-21 on [.5,2], relative 2^-21 outside.
    // The corresponding upper/lower error envelopes are monotone.
    const float2 error = max(abs(logs), 1.0.xx) * 9.5367431640625e-7;
    const float2 logarithm = float2((asuint(base.x) & 0x7fffffffu) == 0u ? asfloat(0xff800000u)
        : ToneDown(logs.x - error.x), ToneUp(logs.y + error.y));
    const float2 powers = ToneMultiply(logarithm, exponent);
    const float2 result = exp2(powers);
    return float2((asuint(result.x) & 0x7fffffffu) == 0u ? 0.0 : max(0.0, ToneDown(result.x * (1.0 - 1.9073486328125e-6))),
        ToneUp(result.y * (1.0 + 1.9073486328125e-6)));
}

struct ToneRgbBounds { float3 low; float3 high; };

#endif

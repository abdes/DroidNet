//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_TONE_MAPPING_FAST_BOUNDS_HLSLI
#define VORTEX_TONE_MAPPING_FAST_BOUNDS_HLSLI

#include "Vortex/Services/PostProcess/ToneMappingBounds.hlsli"

// Sufficient enclosure for the ordinary SDR domain. The constant arithmetic
// allowances include endpoint evaluation and presentation; their derivation
// and exact-rational controls live in VerifyToneMappingFastBounds.py.
static bool ToneDisplayQuickBounds(float4 low, float4 high, float gain,
    uint mapper, float gamma, bool background_enabled, float3 background,
    uint2 pixel, out ToneRgbBounds result)
{
    result.low = 0.0.xxx; result.high = 1.0.xxx;
    if (!isfinite(gamma)) return false;
    const uint3 low_bits = asuint(low.rgb) & 0x7fffffffu.xxx;
    const uint3 high_bits = asuint(high.rgb) & 0x7fffffffu.xxx;
    const uint gain_bits = asuint(gain) & 0x7fffffffu;
    [unroll] for (uint c = 0u; c < 3u; ++c) {
        if (low_bits[c] != 0u && low_bits[c] < 0x00800000u) low[c] = 0.0;
        if (high_bits[c] != 0u && high_bits[c] < 0x00800000u) high[c] = asfloat(0x00800000u);
    }
    const bool tiny_gain = gain_bits != 0u && gain_bits < 0x00800000u;
    const float gain_low = tiny_gain ? 0.0 : gain;
    const float gain_high = tiny_gain ? asfloat(0x00800000u) : gain;
    float3 minimum = max(0.0.xxx, low.rgb / max(high.a, 1e-6) * gain_low);
    float3 maximum = max(0.0.xxx, high.rgb / max(low.a, 1e-6) * gain_high);
    if (!all(isfinite(maximum)) || any(maximum > 1.2676506002282294e30)) return false;
    // Include normalization/exposure and, for ACES, the positive input matrix.
    if (mapper == 1u) {
        minimum = mul(kAcesInputMatrix, minimum);
        maximum = mul(kAcesInputMatrix, maximum);
    }
    minimum = max(0.0.xxx, minimum * (1.0 - 3.814697265625e-6) - 9.4039548065783e-38);
    maximum = maximum * (1.0 + 3.814697265625e-6) + 9.4039548065783e-38;
    if (mapper == 1u) {
        const float3 curve_low = AcesResponse(minimum) - 6.103515625e-5;
        const float3 curve_high = AcesResponse(maximum) + 6.103515625e-5;
        [unroll] for (uint row = 0u; row < 3u; ++row) {
            const float3 positive = max(kAcesOutputMatrix[row], 0.0.xxx);
            const float3 negative = min(kAcesOutputMatrix[row], 0.0.xxx);
            minimum[row] = saturate(dot(positive, curve_low) + dot(negative, curve_high) - 7.62939453125e-6);
            maximum[row] = saturate(dot(positive, curve_high) + dot(negative, curve_low) + 7.62939453125e-6);
        }
    } else if (mapper == 2u) {
        minimum = max(0.0.xxx, Filmic(minimum) - 2.44140625e-4);
        maximum = Filmic(maximum) + 2.44140625e-4;
    } else if (mapper == 3u) {
        minimum = saturate(Reinhard(minimum) - 3.814697265625e-6);
        maximum = saturate(Reinhard(maximum) + 3.814697265625e-6);
    } else {
        minimum = saturate(minimum); maximum = saturate(maximum);
    }
    const bool3 exact_black = maximum == 0.0.xxx;
    if (gamma >= 1.0 && gamma <= 4.0) {
        minimum = max(0.0.xxx, pow(minimum, 1.0 / gamma) - 3.0517578125e-5);
        maximum = pow(maximum, 1.0 / gamma) + 3.0517578125e-5;
    } else {
        const float2 exponent = ToneReciprocal(max(gamma, 1e-4).xx);
        [unroll] for (uint c = 0u; c < 3u; ++c) {
            const float2 value = TonePower(float2(minimum[c], maximum[c]), exponent);
            minimum[c] = value.x; maximum[c] = value.y;
        }
    }
    [unroll] for (uint c = 0u; c < 3u; ++c)
        if (exact_black[c]) { minimum[c] = 0.0; maximum[c] = 0.0; }
    if (background_enabled) {
        // All decoded SDR values are bounded by one. Preserve the repeated
        // foreground in the exact blend; add its FP32 operation allowance.
        float3 linear_low = max(0.0.xxx, SrgbToLinear(minimum) - 3.0517578125e-5);
        float3 linear_high = SrgbToLinear(maximum) + 3.0517578125e-5;
        [unroll] for (uint c = 0u; c < 3u; ++c)
            if (exact_black[c]) { linear_low[c] = 0.0; linear_high[c] = 0.0; }
        const float3 base = saturate(background);
        const float3 low0 = base + (linear_low - base) * low.a;
        const float3 low1 = base + (linear_low - base) * high.a;
        const float3 high0 = base + (linear_high - base) * low.a;
        const float3 high1 = base + (linear_high - base) * high.a;
        minimum = max(0.0.xxx, LinearToSrgb(max(0.0.xxx, min(low0, low1) - 3.814697265625e-6)) - 3.0517578125e-5);
        maximum = min(1.0.xxx, LinearToSrgb(max(high0, high1) + 3.814697265625e-6) + 3.0517578125e-5);
    }
    const float dither = DitherBayer4x4(pixel) / 255.0;
    result.low = saturate(minimum + dither - 4.76837158203125e-7);
    result.high = saturate(maximum + dither + 4.76837158203125e-7);
    return all(isfinite(result.low)) && all(isfinite(result.high));
}

#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_RADIANCE_RANGE_HLSLI
#define VORTEX_HDR_RADIANCE_RANGE_HLSLI

// Completed-status failure bits: nonfinite=1, FP16 headroom=2, unsupported
// scene radiance=32. Small positive components have no hard floor here;
// image/meter budgets determine required preservation.
uint ClassifyHdrStoreRange(float4 value, float pre_exposure, uint fp16_store)
{
    if (!all(isfinite(value)) || !isfinite(pre_exposure)) return 1u;
    const uint3 bits = asuint(value.rgb);
    const uint3 magnitude = bits & 0x7fffffffu.xxx;
    const bool negative = any(and(bits != magnitude, magnitude != 0u.xxx));
    const bool valid_scale = pre_exposure >= exp2(-32.0)
        && pre_exposure <= exp2(32.0);
    // Scaling a binary32 P by 2^32 is exact throughout the supported P domain
    // (the limit spans [1,2^64]). Comparing in stored units avoids reciprocal
    // rounding that could hide the adjacent value above the scene boundary.
    const bool unsupported = negative || !valid_scale
        || any(abs(value.rgb) > exp2(32.0) * pre_exposure);
    return (unsupported ? 32u : 0u)
        | (fp16_store != 0u && any(abs(value.rgb) > 16376.0) ? 2u : 0u);
}

#endif

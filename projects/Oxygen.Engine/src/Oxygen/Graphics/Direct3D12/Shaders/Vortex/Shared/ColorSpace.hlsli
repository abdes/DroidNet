//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SHARED_COLORSPACE_HLSLI
#define OXYGEN_VORTEX_SHARED_COLORSPACE_HLSLI

float3 SrgbToLinear(float3 c)
{
    // IEC 61966-2-1:1999
    c = saturate(c);
    const float3 lo = c / 12.92;
    const float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(hi, lo, step(c, 0.04045));
}

float3 LinearToSrgb(float3 c)
{
    c = max(c, 0.0);
    const float3 lo = c * 12.92;
    const float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return saturate(lerp(hi, lo, step(c, 0.0031308)));
}

#endif

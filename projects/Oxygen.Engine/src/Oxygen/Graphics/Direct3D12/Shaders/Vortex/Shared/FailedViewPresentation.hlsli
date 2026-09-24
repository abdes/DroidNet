//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_FAILED_VIEW_PRESENTATION_HLSLI
#define OXYGEN_VORTEX_FAILED_VIEW_PRESENTATION_HLSLI

// Display-space failure marker, independent of scene exposure and HDR metering.
static float4 FailedViewColor(float2 pixel)
{
    const uint stripe = (uint(pixel.x + pixel.y) / 16u) & 1u;
    return float4(stripe != 0u ? float3(0.35f, 0.025f, 0.08f)
                              : float3(0.075f, 0.015f, 0.025f), 1.0f);
}
#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_PASSES_FORWARD_FORWARDPBR_HLSLI
#define OXYGEN_PASSES_FORWARD_FORWARDPBR_HLSLI

#include "Vortex/Shared/Math.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"

// -----------------------------------------------------------------------------
// PBR helpers (metallic-roughness, GGX)
// -----------------------------------------------------------------------------

float3 SafeNormalize(float3 v)
{
    const float len_sq = dot(v, v);
    if (len_sq <= 1e-20) {
        return float3(0.0, 0.0, 0.0);
    }
    return v * rsqrt(len_sq);
}

#include "Vortex/Shared/ColorSpace.hlsli"

float3 DecodeNormalTS(float3 n)
{
    // Normal maps are typically stored as [0..1]; remap to [-1..1].
    n = n * 2.0 - 1.0;
    // Re-normalize after remap.
    return SafeNormalize(n);
}

#endif // OXYGEN_PASSES_FORWARD_FORWARDPBR_HLSLI

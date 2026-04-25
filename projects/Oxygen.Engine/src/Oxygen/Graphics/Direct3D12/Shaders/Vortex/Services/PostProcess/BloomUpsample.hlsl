//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Shared/FullscreenTriangle.hlsli"

[shader("pixel")]
float4 VortexBloomUpsamplePS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    return float4(input.uv.yx, 0.0f, 1.0f);
}

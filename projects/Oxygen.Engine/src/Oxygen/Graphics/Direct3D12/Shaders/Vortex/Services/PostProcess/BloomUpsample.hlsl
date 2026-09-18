//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Shared/FullscreenTriangle.hlsli"

[shader("pixel")]
float4 VortexBloomUpsamplePS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    // TODO(exposure, owned bloom): replace this inactive placeholder before
    // dispatch. Combine only matching-P radiance, inherit the view HDR format
    // and include cumulative filtering/addition error in final qualification.
    // Owner: design/vortex/lld/post-process-service.md; https://github.com/abdes/DroidNet/issues/12
    return float4(input.uv.yx, 0.0f, 1.0f);
}

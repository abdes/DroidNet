//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Shared/FullscreenTriangle.hlsli"

[shader("vertex")]
VortexFullscreenTriangleOutput VortexFogPassVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexFogPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const float fog_alpha = saturate(input.uv.y) * 0.0f;
    return float4(0.0f, 0.0f, 0.0f, fog_alpha);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Shared/FullscreenTriangle.hlsli"

[shader("vertex")]
VortexFullscreenTriangleOutput VortexSkyPassVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexSkyPassPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const float3 sky_color = float3(input.uv.x, input.uv.y, 1.0f - input.uv.y) * 0.0f;
    return float4(sky_color, 0.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexIblIrradianceCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    (void)dispatch_id;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexIblPrefilterCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    (void)dispatch_id;
}

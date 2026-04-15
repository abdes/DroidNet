//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_FULLSCREENTRIANGLE_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_FULLSCREENTRIANGLE_HLSLI

struct VortexFullscreenTriangleOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static inline VortexFullscreenTriangleOutput GenerateVortexFullscreenTriangle(
    uint vertex_id)
{
    VortexFullscreenTriangleOutput output;

    const float2 corner = float2(
        vertex_id == 1u ? 3.0f : -1.0f,
        vertex_id == 2u ? -3.0f : 1.0f);

    output.position = float4(corner, 0.0f, 1.0f);
    output.uv = float2((corner.x + 1.0f) * 0.5f, (1.0f - corner.y) * 0.5f);
    return output;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_FULLSCREENTRIANGLE_HLSLI

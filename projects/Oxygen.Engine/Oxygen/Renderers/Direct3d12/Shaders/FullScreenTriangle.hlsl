//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

struct VSOutput
{
    noperspective float4 position : SV_POSITION;
    noperspective float2 uv : TEXCOORD0;
};

[RootSignature("")]
[shader("vertex")]
VSOutput VS(uint vertex_idx : SV_VertexID)
{
    VSOutput output;
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0)
    };
    output.position = float4(positions[vertex_idx], 0.0f, 1.0f);
    output.uv = positions[vertex_idx] * 0.5 + 0.5;
    return output;
}

[RootSignature("")]
[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0
{
    return float4(input.uv, 0.0f, 1.0f);
}

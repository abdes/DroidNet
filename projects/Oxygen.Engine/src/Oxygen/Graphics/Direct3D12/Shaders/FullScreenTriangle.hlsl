//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};
struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

[RootSignature("")]
[shader("vertex")]
VSOutput VS(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}

[RootSignature("")]
[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    return float4(input.color, 1.0);
}

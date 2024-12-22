//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

struct VSOutput
{
  noperspective float4 position : SV_POSITION;
  noperspective float2 uv : TEXCOORD;
};

VSOutput main(in uint vertex_idx : SV_VertexID)
{
  VSOutput output;
  float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
  output.position = float4(positions[vertex_idx], 0.0f, 1.0f);
  output.uv = positions[vertex_idx] * 0.5 + 0.5;
  return output;
}

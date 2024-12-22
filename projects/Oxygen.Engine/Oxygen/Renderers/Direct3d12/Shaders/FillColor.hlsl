//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// FillColor.hlsl
// A simple pixel shader that fills the screen with a hardcoded color.

struct PSInput
{
  float4 position : SV_POSITION;
};

float4 main(PSInput input) : SV_TARGET
{
  // Hardcoded fill color (e.g., red)
  return float4(1.0f, 0.0f, 0.0f, 1.0f);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardWireframe_PS.hlsl
//! @brief Unlit wireframe pixel shader (constant color).

#include "Vortex/Contracts/Definitions/MaterialFlags.hlsli"
#include "Vortex/Contracts/Draw/DrawHelpers.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Contracts/Draw/MaterialShadingConstants.hlsli"
#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

// Define vertex structure to satisfy BindlessHelpers.hlsl defaults.
struct Vertex {
  float3 position;
  float3 normal;
  float2 texcoord;
  float3 tangent;
  float3 bitangent;
  float4 color;
};

#include "Core/Bindless/BindlessHelpers.hlsl"

// Mesh shaders take their draw index from SV_StartInstanceLocation (see
// Vortex/Contracts/Draw/DrawHelpers.hlsli); they read only the pass constants
// root constant.
cbuffer RootConstants : register(b2, space0)
{
    uint g_PassConstantsIndex : packoffset(c0.y);
}

// Vertex shader output / Pixel shader input (must match ForwardMesh_VS.hlsl)
struct VSOutput {
  float4 position : SV_POSITION;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  float3 world_pos : TEXCOORD1;
  float3 world_normal : NORMAL;
  float3 world_tangent : TANGENT;
  float3 world_bitangent : BINORMAL;
  nointerpolation uint draw_index : DRAW_INDEX;
};

struct WireframePassConstants {
  float4 wire_color;
  float write_pre_exposed;
  float3 padding;
};

[shader("pixel")] float4 PS(VSOutput input)
  : SV_Target0
{
#ifdef ALPHA_TEST
  const SamplerState linear_sampler = SamplerDescriptorHeap[0];
  ApplyMaskedAlphaClip(
    EvaluateMaskedAlphaTest(input.uv, input.draw_index, linear_sampler));
#endif // ALPHA_TEST

  float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float write_pre_exposed = 1.0f;
  if (BX_IsValidSlot(g_PassConstantsIndex)) {
    StructuredBuffer<WireframePassConstants> constants
      = ResourceDescriptorHeap[g_PassConstantsIndex];
    const WireframePassConstants pc = constants[0];
    color = pc.wire_color;
    write_pre_exposed = pc.write_pre_exposed;
  }

  // PHYSICAL BYPASS: Divide by exposure when requested.
  // This keeps unlit debug lines stable in HDR paths.
  if (write_pre_exposed > 0.5f) {
    color.rgb *= GetPreExposure();
  }

  return color;
}

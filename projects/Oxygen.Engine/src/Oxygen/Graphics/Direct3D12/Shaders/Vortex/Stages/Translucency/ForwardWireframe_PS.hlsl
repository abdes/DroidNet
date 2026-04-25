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
#include "Vortex/Contracts/View/ViewColorHelpers.hlsli"
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

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0)
{
  uint g_DrawIndex;
  uint g_PassConstantsIndex;
};

// Vertex shader output / Pixel shader input (must match ForwardMesh_VS.hlsl)
struct VSOutput {
  float4 position : SV_POSITION;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
  float3 world_pos : TEXCOORD1;
  float3 world_normal : NORMAL;
  float3 world_tangent : TANGENT;
  float3 world_bitangent : BINORMAL;
};

struct WireframePassConstants {
  float4 wire_color;
  float apply_exposure_compensation;
  float3 padding;
};

[shader("pixel")] float4 PS(VSOutput input)
  : SV_Target0
{
#ifdef ALPHA_TEST
  const SamplerState linear_sampler = SamplerDescriptorHeap[0];
  ApplyMaskedAlphaClip(
    EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler));
#endif // ALPHA_TEST

  float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float apply_exposure_compensation = 1.0f;
  if (BX_IsValidSlot(g_PassConstantsIndex)) {
    ConstantBuffer<WireframePassConstants> pc
      = ResourceDescriptorHeap[g_PassConstantsIndex];
    color = pc.wire_color;
    apply_exposure_compensation = pc.apply_exposure_compensation;
  }

  // PHYSICAL BYPASS: Divide by exposure when requested.
  // This keeps unlit debug lines stable in HDR paths.
  if (apply_exposure_compensation > 0.5f) {
    color.rgb /= max(GetExposure(), 1e-6f);
  }

  return color;
}

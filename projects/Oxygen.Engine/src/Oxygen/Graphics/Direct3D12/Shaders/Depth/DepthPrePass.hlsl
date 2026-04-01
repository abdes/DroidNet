//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - The entire heap is mapped to a single descriptor table covering
// t0-unbounded, space0.
// - Resources are intermixed in the heap (structured indirection + geometry
// buffers).
// - DrawMetadata structured buffer occupies a dynamic heap slot; its slot
//   is published each frame via DrawFrameBindings.
// - Uses ResourceDescriptorHeap for direct heap access with proper type
// casting.
// - The root signature uses one table (t0-unbounded, space0) + direct CBVs.
//   See MainModule.cpp and CommandRecorder.cpp for details.

#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaskedAlphaTest.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/ViewConstants.hlsli"

#include "MaterialFlags.hlsli"

struct VertexData {
  float3 position;
  float3 normal;
  float2 texcoord;
  float3 tangent;
  float3 bitangent;
  float4 color;
};

#define BX_VERTEX_TYPE VertexData
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
  uint g_DrawIndex;
  uint g_PassConstantsIndex;
}

struct VS_OUTPUT_DEPTH {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

[shader("vertex")] VS_OUTPUT_DEPTH VS(
  uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
  VS_OUTPUT_DEPTH output;
  output.position = float4(0, 0, 0, 1);
  output.uv = float2(0, 0);

  const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();

  DrawMetadata meta;
  if (!BX_LoadDrawMetadata(
        draw_bindings.draw_metadata_slot, g_DrawIndex, meta)) {
    return output;
  }

  const uint actual_vertex_index = BX_ResolveVertexIndex(meta, vertexID);
  const VertexData v
    = BX_LoadVertex(meta.vertex_buffer_index, actual_vertex_index);
  output.uv = v.texcoord;

  const float4x4 world_matrix
    = BX_LoadInstanceWorldMatrix(draw_bindings.transforms_slot,
      draw_bindings.instance_data_slot, meta, instanceID);
  const float4 world_pos = mul(world_matrix, float4(v.position, 1.0f));
  const float4 view_pos = mul(view_matrix, world_pos);
  output.position = mul(projection_matrix, view_pos);
  return output;
}

[shader("pixel")] void PS(
#ifdef ALPHA_TEST
    VS_OUTPUT_DEPTH input
#else
    VS_OUTPUT_DEPTH
#endif
)
{
#ifdef ALPHA_TEST
  const SamplerState linear_sampler = SamplerDescriptorHeap[0];
  const MaskedAlphaTestResult alpha_test
    = EvaluateMaskedAlphaTest(input.uv, g_DrawIndex, linear_sampler);
  if (!alpha_test.has_material_data || !alpha_test.alpha_test_enabled) {
    return;
  }
  ApplyMaskedAlphaClip(alpha_test);
#endif
}

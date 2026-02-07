//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/SceneConstants.hlsli"

// Root constants b2 (shared root param index with engine)
//   g_DrawIndex          : per-draw payload
//   g_PassConstantsIndex : heap index of a CBV holding pass constants
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct GpuDebugDrawPassConstants
{
    float2 mouse_down_position;
    uint mouse_down_valid;
    uint pad0;
};

struct GpuDebugLine
{
    float4 worldPos0;
    float4 worldPos1;
    float4 colorAlpha0;
    float4 colorAlpha1;
};

struct VertexOutput
{
    float4 position    : SV_POSITION;
    float4 colorAlpha  : TEXCOORD0;
};

[shader("vertex")]
VertexOutput VS(
    uint vertexID: SV_VertexID,
    uint instanceID : SV_InstanceID
)
{
    VertexOutput output;

    StructuredBuffer<GpuDebugLine> gpuDebugLineBuffer = ResourceDescriptorHeap[bindless_gpu_debug_line_slot];
    GpuDebugLine debugLine = gpuDebugLineBuffer[instanceID];

    StructuredBuffer<GpuDebugDrawPassConstants> pass_buffer = ResourceDescriptorHeap[g_PassConstantsIndex];
    GpuDebugDrawPassConstants pass = pass_buffer[g_DrawIndex];

    float4 vertexPosition = vertexID == 0 ? debugLine.worldPos0   : debugLine.worldPos1;
    float4 vertexColor    = vertexID == 0 ? debugLine.colorAlpha0 : debugLine.colorAlpha1;

    output.position  = mul(projection_matrix, mul(view_matrix, vertexPosition));
    output.colorAlpha= vertexColor;
    output.colorAlpha.a += (float)pass.mouse_down_valid * 0.0f;

    return output;
}

[shader("pixel")]
float4 PS(VertexOutput input) : SV_TARGET
{
    return input.colorAlpha;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Draws a view's stored GPU debug lines, one instance per line, with the
// counters as indirect draw arguments.

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Services/Diagnostics/GpuDebug.hlsli"

// g_PassConstantsIndex is the view's GpuDebugBindings record.
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : TEXCOORD0;
};

[shader("vertex")]
VertexOutput VortexGpuDebugDrawVS(
    uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    VertexOutput output = (VertexOutput)0;
    const GpuDebugBindings debug = LoadGpuDebugBindings(g_PassConstantsIndex);
    if (debug.line_buffer_srv_slot == K_INVALID_BINDLESS_INDEX
        || instance_id >= debug.line_capacity) {
        return output;
    }
    StructuredBuffer<GpuDebugLine> lines
        = ResourceDescriptorHeap[debug.line_buffer_srv_slot];
    const GpuDebugLine debug_line = lines[instance_id];
    const float4 world_position
        = vertex_id == 0u ? debug_line.world_pos0 : debug_line.world_pos1;
    output.position = mul(projection_matrix, mul(view_matrix, world_position));
    output.color = vertex_id == 0u ? debug_line.color0 : debug_line.color1;
    return output;
}

[shader("pixel")]
float4 VortexGpuDebugDrawPS(VertexOutput input) : SV_TARGET
{
    return input.color;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VirtualShadowRasterPassConstants
{
    float alpha_cutoff_default;
    uint schedule_srv_index;
    uint schedule_count_srv_index;
    uint atlas_tiles_per_axis;
    uint draw_page_ranges_srv_index;
    uint draw_page_indices_srv_index;
    uint _pad0;
    uint _pad1;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
};

struct VirtualShadowResolvedScheduleEntry
{
    uint global_page_index;
    uint packed_entry;
    uint atlas_tile_x;
    uint atlas_tile_y;
};

[shader("vertex")]
VS_OUTPUT VS(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    VS_OUTPUT output;
    output.position = float4(0.0, 0.0, 2.0, 1.0);

    if (g_PassConstantsIndex == 0xFFFFFFFFu) {
        return output;
    }

    ConstantBuffer<VirtualShadowRasterPassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass_constants.schedule_srv_index == 0xFFFFFFFFu
        || pass_constants.schedule_count_srv_index == 0xFFFFFFFFu
        || pass_constants.atlas_tiles_per_axis == 0u) {
        return output;
    }

    StructuredBuffer<VirtualShadowResolvedScheduleEntry> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_srv_index];
    StructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_srv_index];
    const uint scheduled_page_count = schedule_count[0];
    if (instance_id >= scheduled_page_count) {
        return output;
    }

    const VirtualShadowResolvedScheduleEntry entry = schedule[instance_id];
    const float atlas_scale = 1.0 / float(pass_constants.atlas_tiles_per_axis);
    const float bias_x =
        (float(entry.atlas_tile_x) * 2.0 + 1.0) * atlas_scale - 1.0;
    const float bias_y =
        1.0 - (float(entry.atlas_tile_y) * 2.0 + 1.0) * atlas_scale;

    static const float2 kLocalVertices[6] = {
        float2(-1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0)
    };

    const float2 atlas_ndc =
        kLocalVertices[min(vertex_id, 5u)] * atlas_scale + float2(bias_x, bias_y);
    output.position = float4(atlas_ndc, 1.0, 1.0);
    return output;
}

[shader("pixel")]
void PS(VS_OUTPUT) {}

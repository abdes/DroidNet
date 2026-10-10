//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Reference draws for ExecuteIndirect. Each quad (six vertices) covers one
// column of a COLUMN_COUNT x 1 R32_UINT target and writes the draw index plus
// one, so a readback shows which draws ran and which root constant each saw.
// A draw of several quads covers the columns after its draw index.

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint COLUMN_COUNT = 8u;
static const uint QUAD_VERTICES = 6u;

struct VertexOutput
{
    float4 position : SV_POSITION;
    nointerpolation uint value : VALUE;
};

[shader("vertex")]
VertexOutput VS(uint vertex_id : SV_VertexID)
{
    static const float2 kCorners[QUAD_VERTICES] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    };
    const uint column = g_DrawIndex + vertex_id / QUAD_VERTICES;
    const float2 corner = kCorners[vertex_id % QUAD_VERTICES];

    VertexOutput output;
    output.position = float4(
        ((float(column) + corner.x) / float(COLUMN_COUNT)) * 2.0f - 1.0f,
        corner.y * 2.0f - 1.0f, 0.0f, 1.0f);
    output.value = g_DrawIndex + 1u;
    return output;
}

[shader("pixel")]
uint PS(VertexOutput input) : SV_Target
{
    return input.value;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Reference draws for the mesh draw index contract: a draw's index arrives in
// SV_StartInstanceLocation, whether the draw is direct or indirect. Each quad
// (six vertices) covers one column of a COLUMN_COUNT x 1 R32_UINT target and
// writes the draw index plus one, so a readback shows which draws ran and
// which index each saw.

static const uint COLUMN_COUNT = 8u;
static const uint QUAD_VERTICES = 6u;

struct VertexOutput
{
    float4 position : SV_POSITION;
    nointerpolation uint value : VALUE;
};

[shader("vertex")]
VertexOutput VS(uint vertex_id : SV_VertexID,
    uint draw_index : SV_StartInstanceLocation)
{
    static const float2 kCorners[QUAD_VERTICES] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    };
    const float2 corner = kCorners[vertex_id % QUAD_VERTICES];

    VertexOutput output;
    output.position = float4(
        ((float(draw_index) + corner.x) / float(COLUMN_COUNT)) * 2.0f - 1.0f,
        corner.y * 2.0f - 1.0f, 0.0f, 1.0f);
    output.value = draw_index + 1u;
    return output;
}

[shader("pixel")]
uint PS(VertexOutput input) : SV_Target
{
    return input.value;
}

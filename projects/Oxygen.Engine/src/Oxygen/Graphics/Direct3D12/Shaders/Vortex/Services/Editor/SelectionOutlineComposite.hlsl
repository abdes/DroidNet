//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Selection outline composite: draws a screen-space band of `radius` pixels
// around the outline mask over the view output, anti-aliased by distance,
// brighter for the active level and dimmed where the outlined surface is
// hidden behind other geometry.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Shared/FullscreenTriangle.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct SelectionOutlineCompositeConstants
{
    float4 color;
    float4 active_color;
    uint mask_srv;
    float radius;
    float occluded_alpha;
    uint pad0;
};

static const int kMaxRadius = 3;

[shader("vertex")]
VortexFullscreenTriangleOutput VortexSelectionOutlineCompositeVS(
    uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexSelectionOutlineCompositePS(VortexFullscreenTriangleOutput input)
    : SV_Target0
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        discard;
    }
    StructuredBuffer<SelectionOutlineCompositeConstants> constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const SelectionOutlineCompositeConstants pc = constants[0];
    Texture2D<float2> mask = ResourceDescriptorHeap[pc.mask_srv];

    const int2 pixel = int2(input.position.xy);
    // Interior pixels keep the scene: the outline is a band outside the shape.
    if (mask.Load(int3(pixel, 0)).x > 0.0f) {
        discard;
    }

    const int reach = min(kMaxRadius, (int)ceil(pc.radius));
    float nearest = 1.0e9f;
    float level = 0.0f;
    float visible = 0.0f;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            // Out-of-range loads return zero: the band stops at the edges.
            const float2 m = mask.Load(int3(pixel + int2(dx, dy), 0));
            if (m.x <= 0.0f) {
                continue;
            }
            nearest = min(nearest, length(float2(dx, dy)));
            level = max(level, m.x);
            visible = max(visible, m.y);
        }
    }

    const float coverage = saturate(pc.radius + 0.5f - nearest);
    if (coverage <= 0.0f) {
        discard;
    }

    float4 color = level > 0.75f ? pc.active_color : pc.color;
    color.a *= coverage * (visible > 0.5f ? 1.0f : pc.occluded_alpha);
    return color;
}

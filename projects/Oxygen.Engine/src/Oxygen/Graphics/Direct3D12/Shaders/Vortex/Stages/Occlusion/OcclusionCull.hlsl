//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Per-draw culling of one view: one thread per draw writes the draw's
// visibility bits.
//
// A draw is in the frustum when its oriented box intersects the clip volume
// and its projected rectangle, clipped to the view rect, contains a pixel
// center. A box crossing the near plane always passes the coverage test.
// Occlusion is not tested yet, so every draw in the frustum is drawn in
// phase 1 and is visible.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Draw/DrawCullRecord.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Draw/DrawVisibility.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint OCCLUSION_CULL_GROUP_SIZE = 64u;

// Corners with w at or below this are on or behind the near plane.
static const float OCCLUSION_CULL_NEAR_W_EPSILON = 1.0e-6f;

// Widens the projected rectangle, in pixels, to cover the rasterizer's
// sub-pixel vertex snapping and the float error of the corner transform.
static const float OCCLUSION_CULL_COVERAGE_MARGIN_PX = 1.0f / 64.0f;

// Mirrors oxygen::vortex::occlusion::internal::OcclusionCullPassConstants.
struct OcclusionCullPassConstants
{
    float4x4 view_projection;
    // Viewport origin and size, in pixels.
    float4 viewport;
    // Rasterized pixel rect: min x, min y, max x, max y (exclusive).
    float4 clip_rect;
    uint draw_metadata_srv;
    uint cull_records_srv;
    uint worlds_srv;
    uint visibility_uav;
    uint draw_count;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct ClipBox
{
    float4 center;
    float4 axes[3];
};

ClipBox MakeClipBox(
    OcclusionCullPassConstants c, DrawCullRecord record, DrawMetadata metadata)
{
    float4x4 to_clip = c.view_projection;
    if ((record.flags & DRAW_CULL_WORLD_SPACE_BOX) == 0u
        && c.worlds_srv != K_INVALID_BINDLESS_INDEX)
    {
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[c.worlds_srv];
        to_clip = mul(c.view_projection, worlds[metadata.transform_index]);
    }

    ClipBox box;
    box.center = mul(to_clip, float4(record.box_center, 1.0f));
    box.axes[0] = mul(to_clip, float4(record.box_extent.x, 0.0f, 0.0f, 0.0f));
    box.axes[1] = mul(to_clip, float4(0.0f, record.box_extent.y, 0.0f, 0.0f));
    box.axes[2] = mul(to_clip, float4(0.0f, 0.0f, record.box_extent.z, 0.0f));
    return box;
}

float4 BoxCorner(ClipBox box, uint corner)
{
    const float3 signs = float3((corner & 1u) != 0u ? 1.0f : -1.0f,
        (corner & 2u) != 0u ? 1.0f : -1.0f,
        (corner & 4u) != 0u ? 1.0f : -1.0f);
    return box.center + signs.x * box.axes[0] + signs.y * box.axes[1]
        + signs.z * box.axes[2];
}

// True when the pixel-space interval [lo, hi] contains a pixel center of the
// rasterized range [range_lo, range_hi).
bool ContainsPixelCenter(float lo, float hi, float range_lo, float range_hi)
{
    const float first = ceil(max(lo, range_lo + 0.5f) - 0.5f);
    const float last = floor(min(hi, range_hi - 0.5f) - 0.5f);
    return first <= last;
}

bool IsInFrustum(OcclusionCullPassConstants c, ClipBox box)
{
    // Outside when all corners are beyond the same clip plane.
    uint outside_all = 0x3Fu;
    bool crosses_near = false;
    float2 ndc_min = float2(1.0e30f, 1.0e30f);
    float2 ndc_max = float2(-1.0e30f, -1.0e30f);
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const float4 p = BoxCorner(box, corner);
        uint outside = 0u;
        outside |= p.x < -p.w ? 0x01u : 0u;
        outside |= p.x > p.w ? 0x02u : 0u;
        outside |= p.y < -p.w ? 0x04u : 0u;
        outside |= p.y > p.w ? 0x08u : 0u;
        outside |= p.z < 0.0f ? 0x10u : 0u;
        outside |= p.z > p.w ? 0x20u : 0u;
        outside_all &= outside;

        if (p.w <= OCCLUSION_CULL_NEAR_W_EPSILON)
        {
            crosses_near = true;
        }
        else
        {
            const float2 ndc = p.xy / p.w;
            ndc_min = min(ndc_min, ndc);
            ndc_max = max(ndc_max, ndc);
        }
    }

    if (outside_all != 0u)
    {
        return false;
    }
    if (crosses_near)
    {
        return true;
    }

    // NDC y points up; pixel y points down.
    const float2 origin = c.viewport.xy;
    const float2 size = c.viewport.zw;
    const float2 pixel_min = origin
        + float2(ndc_min.x * 0.5f + 0.5f, 0.5f - ndc_max.y * 0.5f) * size
        - OCCLUSION_CULL_COVERAGE_MARGIN_PX;
    const float2 pixel_max = origin
        + float2(ndc_max.x * 0.5f + 0.5f, 0.5f - ndc_min.y * 0.5f) * size
        + OCCLUSION_CULL_COVERAGE_MARGIN_PX;
    return ContainsPixelCenter(
               pixel_min.x, pixel_max.x, c.clip_rect.x, c.clip_rect.z)
        && ContainsPixelCenter(
            pixel_min.y, pixel_max.y, c.clip_rect.y, c.clip_rect.w);
}

[shader("compute")]
[numthreads(OCCLUSION_CULL_GROUP_SIZE, 1, 1)]
void VortexOcclusionCullCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    StructuredBuffer<OcclusionCullPassConstants> constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const OcclusionCullPassConstants c = constants[0];
    const uint draw_index = dispatch_id.x;
    if (draw_index >= c.draw_count)
    {
        return;
    }

    StructuredBuffer<DrawCullRecord> records
        = ResourceDescriptorHeap[c.cull_records_srv];
    StructuredBuffer<DrawMetadata> metadata
        = ResourceDescriptorHeap[c.draw_metadata_srv];
    const DrawCullRecord record = records[draw_index];

    bool in_frustum = true;
    if ((record.flags & DRAW_CULL_ALWAYS_VISIBLE) == 0u)
    {
        in_frustum
            = IsInFrustum(c, MakeClipBox(c, record, metadata[draw_index]));
    }

    RWStructuredBuffer<uint> visibility
        = ResourceDescriptorHeap[c.visibility_uav];
    visibility[draw_index] = in_frustum
        ? (DRAW_VISIBILITY_IN_FRUSTUM | DRAW_VISIBILITY_PHASE1_DRAWN
            | DRAW_VISIBILITY_VISIBLE)
        : 0u;
}

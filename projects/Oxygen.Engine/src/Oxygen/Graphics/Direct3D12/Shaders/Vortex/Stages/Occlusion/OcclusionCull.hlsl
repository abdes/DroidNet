//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Two-phase culling of one view: one thread per draw writes the draw's
// visibility bits.
//
// Phase 1 tests the frustum and pixel-center coverage of every draw. A draw
// in the frustum is drawn in phase 1 when it was visible last frame, after a
// history reset, or when occlusion is off.
//
// Phase 2 tests every draw in the frustum against the furthest occlusion
// pyramid built from phase 1 depth. A visible draw not drawn in phase 1 is
// drawn in phase 2, and visibility becomes the draw's history.

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

// Corners with w at or below this are on or behind the eye plane.
static const float OCCLUSION_CULL_NEAR_W_EPSILON = 1.0e-6f;

// Widens the projected rectangle, in pixels, to cover the rasterizer's
// sub-pixel vertex snapping and the float error of the corner transform.
static const float OCCLUSION_CULL_COVERAGE_MARGIN_PX = 1.0f / 64.0f;

// Mirror the flags of oxygen::vortex::occlusion::internal::DrawCullPass.
static const uint OCCLUSION_CULL_OCCLUSION_ENABLED = 1u << 0u;
static const uint OCCLUSION_CULL_HISTORY_VALID = 1u << 1u;
static const uint OCCLUSION_CULL_PYRAMID_VALID = 1u << 2u;

// Mirror the slot words of oxygen::vortex::occlusion::internal::
// HistorySlotAllocator.
static const uint OCCLUSION_CULL_NO_HISTORY_SLOT = 0xFFFFFFFFu;
static const uint OCCLUSION_CULL_FRESH_HISTORY_SLOT = 1u << 31u;

// Mirrors oxygen::vortex::OcclusionCounters, one uint per counter.
static const uint OCCLUSION_STAT_DRAWS = 0u;
static const uint OCCLUSION_STAT_IN_FRUSTUM = 1u;
static const uint OCCLUSION_STAT_COVERAGE_CULLED = 2u;
static const uint OCCLUSION_STAT_HISTORY_SLOTS = 3u;
static const uint OCCLUSION_STAT_PHASE1_DRAWN = 4u;
static const uint OCCLUSION_STAT_PHASE2_DRAWN = 5u;
static const uint OCCLUSION_STAT_OCCLUDED = 6u;
static const uint OCCLUSION_STAT_TRANSLUCENT_CULLED = 7u;
static const uint OCCLUSION_STAT_COUNT = 8u;

// Mirrors oxygen::vortex::occlusion::internal::OcclusionCullPassConstants.
struct OcclusionCullPassConstants
{
    // The matrix the rasterizer uses: projection (jitter included) times view.
    float4x4 view_projection;
    // Viewport origin and size, in pixels.
    float4 viewport;
    // Rasterized pixel rect: min x, min y, max x, max y (exclusive).
    float4 clip_rect;
    // How the view's depth target encodes a clip-space point:
    // x * z / w + y * w + z.
    float4 depth_encoding;
    // Turns an encoded depth e into a linear depth:
    // |(x + y * e) / (z + w * e)|.
    float4 depth_linearize;
    // The pyramid's source rect in pixels: origin x, origin y, width, height.
    uint4 pyramid_source;
    uint draw_metadata_srv;
    uint cull_records_srv;
    uint worlds_srv;
    uint visibility_uav;
    uint history_slots_srv;
    uint history_uav;
    uint stats_uav;
    uint pyramid_srv;
    uint draw_count;
    uint flags;
    float depth_bias;
    uint _pad0;
};

struct ClipBox
{
    float4 center;
    float4 axes[3];
};

OcclusionCullPassConstants LoadPassConstants()
{
    StructuredBuffer<OcclusionCullPassConstants> constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return constants[0];
}

ClipBox MakeClipBox(
    OcclusionCullPassConstants c, DrawCullRecord record, uint draw_index)
{
    float4x4 to_clip = c.view_projection;
    if ((record.flags & DRAW_CULL_WORLD_SPACE_BOX) == 0u
        && c.worlds_srv != K_INVALID_BINDLESS_INDEX)
    {
        StructuredBuffer<DrawMetadata> metadata
            = ResourceDescriptorHeap[c.draw_metadata_srv];
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[c.worlds_srv];
        to_clip = mul(
            c.view_projection, worlds[metadata[draw_index].transform_index]);
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

// The box's projection: its pixel rect and nearest device depth, or a box
// that crosses the near plane.
struct ScreenBox
{
    bool outside;
    bool crosses_near;
    float2 pixel_min;
    float2 pixel_max;
    float nearest_depth;
};

// The depth the view's depth target holds for clip-space point `p`.
float EncodeDepth(OcclusionCullPassConstants c, float4 p)
{
    const float4 e = c.depth_encoding;
    return e.x * (p.z / p.w) + e.y * p.w + e.z;
}

ScreenBox ProjectBox(OcclusionCullPassConstants c, ClipBox box)
{
    // Outside when all corners are beyond the same clip plane.
    uint outside_all = 0x3Fu;
    bool crosses_near = false;
    float2 ndc_min = float2(1.0e30f, 1.0e30f);
    float2 ndc_max = float2(-1.0e30f, -1.0e30f);
    float nearest_depth = -1.0e30f;
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

        // Reversed-Z: the near plane is z = w.
        if (p.w <= OCCLUSION_CULL_NEAR_W_EPSILON || p.z >= p.w)
        {
            crosses_near = true;
        }
        else
        {
            const float2 ndc = p.xy / p.w;
            ndc_min = min(ndc_min, ndc);
            ndc_max = max(ndc_max, ndc);
            nearest_depth = max(nearest_depth, EncodeDepth(c, p));
        }
    }

    ScreenBox screen;
    screen.outside = outside_all != 0u;
    screen.crosses_near = crosses_near;
    // NDC y points up; pixel y points down.
    const float2 origin = c.viewport.xy;
    const float2 size = c.viewport.zw;
    screen.pixel_min = origin
        + float2(ndc_min.x * 0.5f + 0.5f, 0.5f - ndc_max.y * 0.5f) * size
        - OCCLUSION_CULL_COVERAGE_MARGIN_PX;
    screen.pixel_max = origin
        + float2(ndc_max.x * 0.5f + 0.5f, 0.5f - ndc_min.y * 0.5f) * size
        + OCCLUSION_CULL_COVERAGE_MARGIN_PX;
    screen.nearest_depth = nearest_depth;
    return screen;
}

// The pixel centers of [lo, hi] inside the rasterized range
// [range_lo, range_hi), as inclusive pixel indices; empty when first > last.
int2 CoveredPixels(float lo, float hi, float range_lo, float range_hi)
{
    const float first = ceil(max(lo, range_lo + 0.5f) - 0.5f);
    const float last = floor(min(hi, range_hi - 0.5f) - 0.5f);
    return int2(first, last);
}

bool CoversPixelCenter(OcclusionCullPassConstants c, ScreenBox screen)
{
    const int2 x = CoveredPixels(
        screen.pixel_min.x, screen.pixel_max.x, c.clip_rect.x, c.clip_rect.z);
    const int2 y = CoveredPixels(
        screen.pixel_min.y, screen.pixel_max.y, c.clip_rect.y, c.clip_rect.w);
    return x.x <= x.y && y.x <= y.y;
}

// Linear depth of an encoded depth.
float ViewDepth(OcclusionCullPassConstants c, float encoded_depth)
{
    const float4 t = c.depth_linearize;
    return abs((t.x + t.y * encoded_depth) / (t.z + t.w * encoded_depth));
}

// True when every pixel center the box covers lies behind the furthest
// phase 1 depth by more than the relative bias.
bool OccludedByPyramid(OcclusionCullPassConstants c, ScreenBox screen)
{
    // Covered pixel centers. Pixels outside the pyramid's source rect have no
    // depth, so a box reaching them is never occluded.
    const int2 source_min = int2(c.pyramid_source.xy);
    const int2 source_max = source_min + int2(c.pyramid_source.zw) - 1;
    const int2 x = CoveredPixels(
        screen.pixel_min.x, screen.pixel_max.x, c.clip_rect.x, c.clip_rect.z);
    const int2 y = CoveredPixels(
        screen.pixel_min.y, screen.pixel_max.y, c.clip_rect.y, c.clip_rect.w);
    if (x.x > x.y || y.x > y.y || x.x < source_min.x || y.x < source_min.y
        || x.y > source_max.x || y.y > source_max.y)
    {
        return false;
    }

    // Mip-0 texel t reduces source pixels 2t and 2t + 1. Climb until the
    // footprint spans at most 2 x 2 texels; the last mip is at most 2 wide.
    Texture2D<float> pyramid = ResourceDescriptorHeap[c.pyramid_srv];
    uint width;
    uint height;
    uint mip_count;
    pyramid.GetDimensions(0u, width, height, mip_count);
    uint2 lo = uint2(x.x - source_min.x, y.x - source_min.y) >> 1u;
    uint2 hi = uint2(x.y - source_min.x, y.y - source_min.y) >> 1u;
    uint mip = 0u;
    while (mip + 1u < mip_count && any(hi - lo > 1u))
    {
        lo >>= 1u;
        hi >>= 1u;
        ++mip;
    }

    // Reversed-Z: the furthest occluder has the smallest depth.
    float furthest = 1.0f;
    [unroll]
    for (uint texel = 0u; texel < 4u; ++texel)
    {
        const uint2 at = min(lo + uint2(texel & 1u, texel >> 1u), hi);
        furthest = min(furthest, pyramid.Load(int3(at, mip)));
    }

    // NaN depths compare false, so they never cull.
    return ViewDepth(c, screen.nearest_depth)
        > ViewDepth(c, furthest) * (1.0f + c.depth_bias);
}

void CountStat(OcclusionCullPassConstants c, uint stat, bool counted)
{
    if (c.stats_uav == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }
    const uint count = WaveActiveCountBits(counted);
    if (WaveIsFirstLane() && count != 0u)
    {
        RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[c.stats_uav];
        InterlockedAdd(stats[stat], count);
    }
}

uint LoadHistorySlot(OcclusionCullPassConstants c, uint draw_index)
{
    if (c.history_slots_srv == K_INVALID_BINDLESS_INDEX)
    {
        return OCCLUSION_CULL_NO_HISTORY_SLOT;
    }
    StructuredBuffer<uint> slots = ResourceDescriptorHeap[c.history_slots_srv];
    return slots[draw_index];
}

[shader("compute")]
[numthreads(OCCLUSION_STAT_COUNT, 1, 1)]
void VortexOcclusionStatsClearCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const OcclusionCullPassConstants c = LoadPassConstants();
    RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[c.stats_uav];
    stats[dispatch_id.x] = 0u;
}

[shader("compute")]
[numthreads(OCCLUSION_CULL_GROUP_SIZE, 1, 1)]
void VortexOcclusionPhase1CS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const OcclusionCullPassConstants c = LoadPassConstants();
    const uint draw_index = dispatch_id.x;
    const bool active = draw_index < c.draw_count;

    bool in_planes = false;
    bool in_frustum = false;
    bool has_slot = false;
    bool phase1 = false;
    if (active)
    {
        StructuredBuffer<DrawCullRecord> records
            = ResourceDescriptorHeap[c.cull_records_srv];
        const DrawCullRecord record = records[draw_index];
        if ((record.flags & DRAW_CULL_ALWAYS_VISIBLE) != 0u)
        {
            in_planes = true;
            in_frustum = true;
        }
        else
        {
            const ScreenBox screen
                = ProjectBox(c, MakeClipBox(c, record, draw_index));
            in_planes = !screen.outside;
            in_frustum = in_planes
                && (screen.crosses_near || CoversPixelCenter(c, screen));
        }

        const bool occlusion = (c.flags & OCCLUSION_CULL_OCCLUSION_ENABLED) != 0u;
        const bool history_valid = (c.flags & OCCLUSION_CULL_HISTORY_VALID) != 0u;
        const uint slot = LoadHistorySlot(c, draw_index);
        has_slot = slot != OCCLUSION_CULL_NO_HISTORY_SLOT;
        bool previous = false;
        if (history_valid && has_slot
            && (slot & OCCLUSION_CULL_FRESH_HISTORY_SLOT) == 0u)
        {
            RWStructuredBuffer<uint> history
                = ResourceDescriptorHeap[c.history_uav];
            previous = history[slot] != 0u;
        }
        phase1 = in_frustum && (previous || !history_valid || !occlusion);

        uint bits = 0u;
        bits |= in_frustum ? DRAW_VISIBILITY_IN_FRUSTUM : 0u;
        bits |= phase1 ? DRAW_VISIBILITY_PHASE1_DRAWN : 0u;
        // Without occlusion, phase 1 is final.
        bits |= in_frustum && !occlusion ? DRAW_VISIBILITY_VISIBLE : 0u;
        RWStructuredBuffer<uint> visibility
            = ResourceDescriptorHeap[c.visibility_uav];
        visibility[draw_index] = bits;
    }

    CountStat(c, OCCLUSION_STAT_DRAWS, active);
    CountStat(c, OCCLUSION_STAT_IN_FRUSTUM, in_frustum);
    CountStat(c, OCCLUSION_STAT_COVERAGE_CULLED, in_planes && !in_frustum);
    CountStat(c, OCCLUSION_STAT_HISTORY_SLOTS, has_slot);
    CountStat(c, OCCLUSION_STAT_PHASE1_DRAWN, phase1);
}

[shader("compute")]
[numthreads(OCCLUSION_CULL_GROUP_SIZE, 1, 1)]
void VortexOcclusionPhase2CS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const OcclusionCullPassConstants c = LoadPassConstants();
    const uint draw_index = dispatch_id.x;
    const bool active = draw_index < c.draw_count;

    bool phase2 = false;
    bool occluded = false;
    bool translucent_culled = false;
    if (active)
    {
        RWStructuredBuffer<uint> visibility
            = ResourceDescriptorHeap[c.visibility_uav];
        uint bits = visibility[draw_index];
        const bool in_frustum = (bits & DRAW_VISIBILITY_IN_FRUSTUM) != 0u;

        StructuredBuffer<DrawCullRecord> records
            = ResourceDescriptorHeap[c.cull_records_srv];
        const DrawCullRecord record = records[draw_index];
        bool visible = in_frustum;
        if (in_frustum && (record.flags & DRAW_CULL_ALWAYS_VISIBLE) == 0u
            && (c.flags & OCCLUSION_CULL_PYRAMID_VALID) != 0u)
        {
            const ScreenBox screen
                = ProjectBox(c, MakeClipBox(c, record, draw_index));
            visible = screen.crosses_near || !OccludedByPyramid(c, screen);
        }

        phase2 = visible && (bits & DRAW_VISIBILITY_PHASE1_DRAWN) == 0u;
        occluded = in_frustum && !visible;
        translucent_culled
            = occluded && (record.flags & DRAW_CULL_TRANSPARENT) != 0u;
        bits |= phase2 ? DRAW_VISIBILITY_PHASE2_DRAWN : 0u;
        bits |= visible ? DRAW_VISIBILITY_VISIBLE : 0u;
        visibility[draw_index] = bits;

        const uint slot = LoadHistorySlot(c, draw_index);
        if (slot != OCCLUSION_CULL_NO_HISTORY_SLOT)
        {
            RWStructuredBuffer<uint> history
                = ResourceDescriptorHeap[c.history_uav];
            history[slot & ~OCCLUSION_CULL_FRESH_HISTORY_SLOT]
                = visible ? 1u : 0u;
        }
    }

    CountStat(c, OCCLUSION_STAT_PHASE2_DRAWN, phase2);
    CountStat(c, OCCLUSION_STAT_OCCLUDED, occluded);
    CountStat(c, OCCLUSION_STAT_TRANSLUCENT_CULLED, translucent_culled);
}

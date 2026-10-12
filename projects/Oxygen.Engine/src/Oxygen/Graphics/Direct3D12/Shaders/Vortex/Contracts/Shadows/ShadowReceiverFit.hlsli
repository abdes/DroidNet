//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SHADOW_RECEIVER_FIT_HLSLI
#define OXYGEN_VORTEX_SHADOW_RECEIVER_FIT_HLSLI

// Pure contract of the directional receiver fit and receiver mask: the inputs
// record of their compute passes and the mask lookup used by caster culling.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

// Mirrors oxygen::vortex::shadows::ShadowReceiverFitInputs.
struct ShadowReceiverFitInputs
{
    // The camera's projection (jitter included) times view, inverted.
    float4x4 inverse_view_projection;
    float4x4 view_matrix;
    // The view rect in scene-depth pixels: origin x, origin y, width, height.
    float4 viewport;
    uint scene_depth_srv;
    // The view's fitted cascade records, seeded from the CPU's loose records.
    uint cascade_records_uav;
    uint loose_cascade_records_srv;
    uint cascade_count;
    uint bounds_uav;
    uint mask_uav;
    uint draw_cull_records_srv;
    uint draw_metadata_srv;
    uint worlds_srv;
    uint draw_count;
    // The camera near plane: nearer receivers belong to the first cascade.
    float near_plane;
    uint _pad0;
};

// Receivers of one cascade in its loose clip space, as ordered floats.
struct ShadowReceiverBounds
{
    uint min_x;
    uint min_y;
    uint max_x;
    uint max_y;
    uint receiver_count;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

// Each cascade owns one block of the mask buffer: the marked tiles, the
// dilated tiles, then one coarse tile per 8 x 8 dilated tiles.
static const uint SHADOW_RECEIVER_MASK_RESOLUTION = 64u;
static const uint SHADOW_RECEIVER_MASK_COARSE_REDUCTION = 8u;
static const uint SHADOW_RECEIVER_MASK_COARSE_RESOLUTION
    = SHADOW_RECEIVER_MASK_RESOLUTION / SHADOW_RECEIVER_MASK_COARSE_REDUCTION;
static const uint SHADOW_RECEIVER_MASK_TILES
    = SHADOW_RECEIVER_MASK_RESOLUTION * SHADOW_RECEIVER_MASK_RESOLUTION;
static const uint SHADOW_RECEIVER_MASK_MARKED_OFFSET = 0u;
static const uint SHADOW_RECEIVER_MASK_DILATED_OFFSET = SHADOW_RECEIVER_MASK_TILES;
static const uint SHADOW_RECEIVER_MASK_COARSE_OFFSET
    = 2u * SHADOW_RECEIVER_MASK_TILES;
static const uint SHADOW_RECEIVER_MASK_BLOCK = 2u * SHADOW_RECEIVER_MASK_TILES
    + SHADOW_RECEIVER_MASK_COARSE_RESOLUTION
        * SHADOW_RECEIVER_MASK_COARSE_RESOLUTION;

// The shadow-map UV of a cascade clip position: x right, y down.
static inline float2 ShadowReceiverMaskUv(float2 clip_xy)
{
    return float2(clip_xy.x * 0.5f + 0.5f, 0.5f - clip_xy.y * 0.5f);
}

static inline uint2 ShadowReceiverMaskTile(float2 uv)
{
    return uint2(clamp(floor(saturate(uv) * SHADOW_RECEIVER_MASK_RESOLUTION),
        0.0f, float(SHADOW_RECEIVER_MASK_RESOLUTION - 1u)));
}

// True when the shadow-map UV rect [uv_min, uv_max] of a caster touches a
// tile that receives its shadow in cascade `cascade`.
static inline bool ShadowReceiverMaskCovers(
    uint mask_srv, uint cascade, float2 uv_min, float2 uv_max)
{
    if (any(uv_max < 0.0f) || any(uv_min > 1.0f)) {
        return false;
    }
    StructuredBuffer<uint> mask = ResourceDescriptorHeap[mask_srv];
    const uint block = cascade * SHADOW_RECEIVER_MASK_BLOCK;
    const uint2 lo = ShadowReceiverMaskTile(uv_min);
    const uint2 hi = ShadowReceiverMaskTile(uv_max);
    const uint2 coarse_lo = lo / SHADOW_RECEIVER_MASK_COARSE_REDUCTION;
    const uint2 coarse_hi = hi / SHADOW_RECEIVER_MASK_COARSE_REDUCTION;
    [loop]
    for (uint cy = coarse_lo.y; cy <= coarse_hi.y; ++cy) {
        [loop]
        for (uint cx = coarse_lo.x; cx <= coarse_hi.x; ++cx) {
            if (mask[block + SHADOW_RECEIVER_MASK_COARSE_OFFSET
                    + cy * SHADOW_RECEIVER_MASK_COARSE_RESOLUTION + cx]
                == 0u) {
                continue;
            }
            const uint2 tile_lo = max(lo,
                uint2(cx, cy) * SHADOW_RECEIVER_MASK_COARSE_REDUCTION);
            const uint2 tile_hi = min(hi,
                uint2(cx, cy) * SHADOW_RECEIVER_MASK_COARSE_REDUCTION
                    + (SHADOW_RECEIVER_MASK_COARSE_REDUCTION - 1u));
            [loop]
            for (uint y = tile_lo.y; y <= tile_hi.y; ++y) {
                [loop]
                for (uint x = tile_lo.x; x <= tile_hi.x; ++x) {
                    if (mask[block + SHADOW_RECEIVER_MASK_DILATED_OFFSET
                            + y * SHADOW_RECEIVER_MASK_RESOLUTION + x]
                        != 0u) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

#endif // OXYGEN_VORTEX_SHADOW_RECEIVER_FIT_HLSLI

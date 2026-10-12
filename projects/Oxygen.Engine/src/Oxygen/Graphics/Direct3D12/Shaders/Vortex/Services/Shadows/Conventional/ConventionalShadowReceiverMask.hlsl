//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Marks, for each fitted directional cascade, the shadow-map tiles where the
// camera sees a receiver. Caster culling then rejects casters whose shadow
// lands on no marked tile (ShadowReceiverMaskCovers).
//
// Receivers mark the tile they project to, dilated by their sampling reach:
// the normal bias plus the PCF footprint in fitted texels. A coarse tile per
// 8 x 8 tiles lets culling skip empty regions.

#include "Vortex/Services/Shadows/Conventional/ShadowReceiverCommon.hlsli"

static const float SHADOW_RECEIVER_MASK_SAMPLING_TEXELS = 3.0f;

static void MarkTile(ShadowReceiverFitInputs inputs, uint cascade, uint2 tile)
{
    RWStructuredBuffer<uint> mask = ResourceDescriptorHeap[inputs.mask_uav];
    const uint index = cascade * SHADOW_RECEIVER_MASK_BLOCK
        + SHADOW_RECEIVER_MASK_MARKED_OFFSET
        + tile.y * SHADOW_RECEIVER_MASK_RESOLUTION + tile.x;
    if (mask[index] == 0u) {
        InterlockedOr(mask[index], 1u);
    }
}

[shader("compute")]
[numthreads(SHADOW_RECEIVER_LINEAR_GROUP_SIZE, 1, 1)]
void VortexShadowReceiverMaskClearCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ShadowReceiverFitInputs inputs = LoadShadowReceiverFitInputs();
    if (dispatch_id.x >= inputs.cascade_count * SHADOW_RECEIVER_MASK_BLOCK) {
        return;
    }
    RWStructuredBuffer<uint> mask = ResourceDescriptorHeap[inputs.mask_uav];
    mask[dispatch_id.x] = 0u;
}

// One thread per scene-depth pixel of the view.
[shader("compute")]
[numthreads(SHADOW_RECEIVER_SCREEN_GROUP_SIZE, SHADOW_RECEIVER_SCREEN_GROUP_SIZE, 1)]
void VortexShadowReceiverMaskDepthCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ShadowReceiverFitInputs inputs = LoadShadowReceiverFitInputs();
    float3 world_position;
    float view_depth;
    if (!TryLoadReceiverPixel(inputs, dispatch_id.xy, world_position, view_depth)) {
        return;
    }
    [loop]
    for (uint cascade_index = 0u; cascade_index < inputs.cascade_count;
        ++cascade_index) {
        const VortexShadowCascadeBinding cascade
            = LoadReceiverCascade(inputs, cascade_index);
        if (!ReceiverSamplesCascade(inputs, cascade, view_depth)) {
            continue;
        }
        const float2 clip
            = mul(cascade.light_view_projection, float4(world_position, 1.0f)).xy;
        MarkTile(inputs, cascade_index,
            ShadowReceiverMaskTile(ShadowReceiverMaskUv(clip)));
    }
}

// One thread per draw; a translucent draw marks every tile its box covers.
[shader("compute")]
[numthreads(SHADOW_RECEIVER_LINEAR_GROUP_SIZE, 1, 1)]
void VortexShadowReceiverMaskTranslucentCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ShadowReceiverFitInputs inputs = LoadShadowReceiverFitInputs();
    float3 corners[8];
    if (!TryLoadTranslucentReceiver(inputs, dispatch_id.x, corners)) {
        return;
    }
    const float2 depth_range = ReceiverViewDepthRange(inputs, corners);
    [loop]
    for (uint cascade_index = 0u; cascade_index < inputs.cascade_count;
        ++cascade_index) {
        const VortexShadowCascadeBinding cascade
            = LoadReceiverCascade(inputs, cascade_index);
        if (!ReceiverRangeSamplesCascade(
                inputs, cascade, depth_range.x, depth_range.y)) {
            continue;
        }
        const float4 rect
            = ReceiverClipRect(cascade.light_view_projection, corners);
        // Clip y points up; mask rows point down.
        const uint2 lo = ShadowReceiverMaskTile(ShadowReceiverMaskUv(rect.xw));
        const uint2 hi = ShadowReceiverMaskTile(ShadowReceiverMaskUv(rect.zy));
        [loop]
        for (uint y = lo.y; y <= hi.y; ++y) {
            [loop]
            for (uint x = lo.x; x <= hi.x; ++x) {
                MarkTile(inputs, cascade_index, uint2(x, y));
            }
        }
    }
}

// One thread per tile of every cascade: a tile is a receiver tile when a
// marked tile lies within the sampling reach.
[shader("compute")]
[numthreads(SHADOW_RECEIVER_LINEAR_GROUP_SIZE, 1, 1)]
void VortexShadowReceiverMaskDilateCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ShadowReceiverFitInputs inputs = LoadShadowReceiverFitInputs();
    if (dispatch_id.x >= inputs.cascade_count * SHADOW_RECEIVER_MASK_TILES) {
        return;
    }
    const uint cascade_index = dispatch_id.x / SHADOW_RECEIVER_MASK_TILES;
    const uint tile_index = dispatch_id.x % SHADOW_RECEIVER_MASK_TILES;
    const int2 tile = int2(tile_index % SHADOW_RECEIVER_MASK_RESOLUTION,
        tile_index / SHADOW_RECEIVER_MASK_RESOLUTION);
    const VortexShadowCascadeBinding cascade
        = LoadReceiverCascade(inputs, cascade_index);
    const float resolution = cascade.inverse_resolution.x > 0.0f
        ? 1.0f / cascade.inverse_resolution.x
        : 1.0f;
    const float tile_size = cascade.world_texel_size * resolution
        / float(SHADOW_RECEIVER_MASK_RESOLUTION);
    const float reach = cascade.normal_bias_m
        + SHADOW_RECEIVER_MASK_SAMPLING_TEXELS * cascade.world_texel_size;
    const int radius = int(ceil(reach / max(tile_size, 1.0e-6f)));

    RWStructuredBuffer<uint> mask = ResourceDescriptorHeap[inputs.mask_uav];
    const uint block = cascade_index * SHADOW_RECEIVER_MASK_BLOCK;
    const int last = int(SHADOW_RECEIVER_MASK_RESOLUTION) - 1;
    uint covered = 0u;
    [loop]
    for (int y = max(tile.y - radius, 0); y <= min(tile.y + radius, last)
        && covered == 0u; ++y) {
        [loop]
        for (int x = max(tile.x - radius, 0); x <= min(tile.x + radius, last);
            ++x) {
            if (mask[block + SHADOW_RECEIVER_MASK_MARKED_OFFSET
                    + uint(y) * SHADOW_RECEIVER_MASK_RESOLUTION + uint(x)]
                != 0u) {
                covered = 1u;
                break;
            }
        }
    }
    mask[block + SHADOW_RECEIVER_MASK_DILATED_OFFSET + tile_index] = covered;
    if (covered != 0u) {
        const uint2 coarse = uint2(tile) / SHADOW_RECEIVER_MASK_COARSE_REDUCTION;
        InterlockedOr(mask[block + SHADOW_RECEIVER_MASK_COARSE_OFFSET
                          + coarse.y * SHADOW_RECEIVER_MASK_COARSE_RESOLUTION
                          + coarse.x],
            1u);
    }
}

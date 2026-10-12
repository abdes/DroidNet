//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SHADOW_RECEIVER_COMMON_HLSLI
#define OXYGEN_VORTEX_SHADOW_RECEIVER_COMMON_HLSLI

// Receivers of the directional receiver fit and mask: the camera's visible
// surfaces from its complete scene depth, plus the boxes of its translucent
// draws, which write no depth but sample directional shadows. Pure helpers:
// every input comes from the ShadowReceiverFitInputs record.

#include "Vortex/Contracts/Draw/DrawCullRecord.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Shadows/ShadowCascadeBinding.hlsli"
#include "Vortex/Contracts/Shadows/ShadowReceiverFit.hlsli"

// g_PassConstantsIndex is the ShadowReceiverFitInputs record.
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint SHADOW_RECEIVER_SCREEN_GROUP_SIZE = 8u;
static const uint SHADOW_RECEIVER_LINEAR_GROUP_SIZE = 64u;

static inline ShadowReceiverFitInputs LoadShadowReceiverFitInputs()
{
    StructuredBuffer<ShadowReceiverFitInputs> inputs
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return inputs[0];
}

static inline VortexShadowCascadeBinding LoadReceiverCascade(
    ShadowReceiverFitInputs inputs, uint cascade)
{
    RWStructuredBuffer<VortexShadowCascadeBinding> cascades
        = ResourceDescriptorHeap[inputs.cascade_records_uav];
    return cascades[cascade];
}

// Whether a receiver at `view_depth` samples `cascade`: the cascade whose
// split it falls in, both cascades inside a transition overlap, and the first
// cascade of a light for receivers nearer than its split.
static inline bool ReceiverSamplesCascade(ShadowReceiverFitInputs inputs,
    VortexShadowCascadeBinding cascade, float view_depth)
{
    return view_depth <= cascade.split_far
        && (view_depth >= cascade.split_near
            || cascade.split_near <= inputs.near_plane);
}

// Whether any part of the view-depth range [depth_min, depth_max] samples
// `cascade`.
static inline bool ReceiverRangeSamplesCascade(ShadowReceiverFitInputs inputs,
    VortexShadowCascadeBinding cascade, float depth_min, float depth_max)
{
    return depth_min <= cascade.split_far
        && (depth_max >= cascade.split_near
            || cascade.split_near <= inputs.near_plane);
}

// The world position of a scene-depth pixel, or false for the background and
// pixels outside the view.
static inline bool TryLoadReceiverPixel(ShadowReceiverFitInputs inputs,
    uint2 offset, out float3 world_position, out float view_depth)
{
    world_position = 0.0f.xxx;
    view_depth = 0.0f;
    if (any(float2(offset) >= inputs.viewport.zw)) {
        return false;
    }
    const uint2 pixel = uint2(inputs.viewport.xy) + offset;
    Texture2D<float> depth_texture = ResourceDescriptorHeap[inputs.scene_depth_srv];
    const float depth = depth_texture.Load(int3(pixel, 0));
    // Reversed-Z: the cleared background is at depth 0.
    if (depth <= 0.0f) {
        return false;
    }
    const float2 uv = (float2(offset) + 0.5f) / inputs.viewport.zw;
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 world = mul(inputs.inverse_view_projection, clip);
    world_position = world.xyz / world.w;
    view_depth = max(0.0f, -mul(inputs.view_matrix, float4(world_position, 1.0f)).z);
    return true;
}

// The world-space corners of translucent draw `draw_index`; false for other
// draws.
static inline bool TryLoadTranslucentReceiver(ShadowReceiverFitInputs inputs,
    uint draw_index, out float3 corners[8])
{
    [unroll]
    for (uint i = 0u; i < 8u; ++i) {
        corners[i] = 0.0f.xxx;
    }
    if (draw_index >= inputs.draw_count
        || inputs.draw_cull_records_srv == K_INVALID_BINDLESS_INDEX) {
        return false;
    }
    StructuredBuffer<DrawCullRecord> records
        = ResourceDescriptorHeap[inputs.draw_cull_records_srv];
    const DrawCullRecord record = records[draw_index];
    if ((record.flags & DRAW_CULL_TRANSPARENT) == 0u) {
        return false;
    }
    float4x4 world = float4x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    if ((record.flags & DRAW_CULL_WORLD_SPACE_BOX) == 0u
        && inputs.worlds_srv != K_INVALID_BINDLESS_INDEX
        && inputs.draw_metadata_srv != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<DrawMetadata> metadata
            = ResourceDescriptorHeap[inputs.draw_metadata_srv];
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[inputs.worlds_srv];
        world = worlds[metadata[draw_index].transform_index];
    }
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const float3 signs = float3((corner & 1u) != 0u ? 1.0f : -1.0f,
            (corner & 2u) != 0u ? 1.0f : -1.0f,
            (corner & 4u) != 0u ? 1.0f : -1.0f);
        corners[corner] = mul(world,
            float4(record.box_center + signs * record.box_extent, 1.0f)).xyz;
    }
    return true;
}

static inline float2 ReceiverViewDepthRange(
    ShadowReceiverFitInputs inputs, float3 corners[8])
{
    float2 range = float2(3.402823466e+38f, 0.0f);
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const float depth = max(0.0f,
            -mul(inputs.view_matrix, float4(corners[corner], 1.0f)).z);
        range = float2(min(range.x, depth), max(range.y, depth));
    }
    return range;
}

// The clip-space xy rect of `corners` under `light_view_projection`, an
// orthographic cascade matrix: min x, min y, max x, max y.
static inline float4 ReceiverClipRect(
    float4x4 light_view_projection, float3 corners[8])
{
    float4 rect = float4(3.402823466e+38f.xx, -3.402823466e+38f.xx);
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const float2 clip
            = mul(light_view_projection, float4(corners[corner], 1.0f)).xy;
        rect = float4(min(rect.xy, clip), max(rect.zw, clip));
    }
    return rect;
}

#endif // OXYGEN_VORTEX_SHADOW_RECEIVER_COMMON_HLSLI

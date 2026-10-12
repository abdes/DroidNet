//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Fits each directional cascade to the receivers the camera sees.
//
// The cascade records enter with the CPU's loose cascades, which cover the
// whole frustum slice of each split. The analysis reduces the receivers of
// each cascade to a rect in its loose clip space; the fit then narrows the
// cascade's light-space xy to that rect plus the sampling margin, and writes
// the fitted matrix and texel size back into the record. Depth range, splits
// and bias stay as the CPU set them.
//
// Fitted extents step through quarter-octaves of the loose extent and their
// centers snap to the fitted texel grid in world-anchored light space, so a
// moving camera does not make shadow edges shimmer.

#include "Vortex/Services/Shadows/Conventional/ShadowReceiverCommon.hlsli"

static const float SHADOW_RECEIVER_FLOAT_MAX = 3.402823466e+38f;
// Shadow-map texels a receiver's sampling reaches beyond its own position:
// the 3x3 PCF footprint plus the receiver-plane and world-texel offsets.
static const float SHADOW_RECEIVER_SAMPLING_TEXELS = 3.0f;
// Fitted extents step through 2^(1/4) of the loose extent.
static const float SHADOW_RECEIVER_EXTENT_STEPS_PER_OCTAVE = 4.0f;

static uint FloatToOrdered(float value)
{
    const uint bits = asuint(value);
    return (bits & 0x80000000u) != 0u ? ~bits : (bits ^ 0x80000000u);
}

static float OrderedToFloat(uint ordered)
{
    return asfloat((ordered & 0x80000000u) != 0u ? (ordered ^ 0x80000000u)
                                                 : ~ordered);
}

static void AccumulateBounds(
    ShadowReceiverFitInputs inputs, uint cascade, float4 rect, uint count)
{
    RWStructuredBuffer<ShadowReceiverBounds> bounds
        = ResourceDescriptorHeap[inputs.bounds_uav];
    InterlockedMin(bounds[cascade].min_x, FloatToOrdered(rect.x));
    InterlockedMin(bounds[cascade].min_y, FloatToOrdered(rect.y));
    InterlockedMax(bounds[cascade].max_x, FloatToOrdered(rect.z));
    InterlockedMax(bounds[cascade].max_y, FloatToOrdered(rect.w));
    InterlockedAdd(bounds[cascade].receiver_count, count);
}

[shader("compute")]
[numthreads(SHADOW_RECEIVER_LINEAR_GROUP_SIZE, 1, 1)]
void VortexShadowReceiverClearCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ShadowReceiverFitInputs inputs = LoadShadowReceiverFitInputs();
    if (dispatch_id.x >= inputs.cascade_count) {
        return;
    }
    StructuredBuffer<VortexShadowCascadeBinding> loose
        = ResourceDescriptorHeap[inputs.loose_cascade_records_srv];
    RWStructuredBuffer<VortexShadowCascadeBinding> cascades
        = ResourceDescriptorHeap[inputs.cascade_records_uav];
    cascades[dispatch_id.x] = loose[dispatch_id.x];
    RWStructuredBuffer<ShadowReceiverBounds> bounds
        = ResourceDescriptorHeap[inputs.bounds_uav];
    ShadowReceiverBounds cleared = (ShadowReceiverBounds)0;
    cleared.min_x = FloatToOrdered(SHADOW_RECEIVER_FLOAT_MAX);
    cleared.min_y = FloatToOrdered(SHADOW_RECEIVER_FLOAT_MAX);
    cleared.max_x = FloatToOrdered(-SHADOW_RECEIVER_FLOAT_MAX);
    cleared.max_y = FloatToOrdered(-SHADOW_RECEIVER_FLOAT_MAX);
    bounds[dispatch_id.x] = cleared;
}

// One thread per scene-depth pixel of the view.
[shader("compute")]
[numthreads(SHADOW_RECEIVER_SCREEN_GROUP_SIZE, SHADOW_RECEIVER_SCREEN_GROUP_SIZE, 1)]
void VortexShadowReceiverDepthCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ShadowReceiverFitInputs inputs = LoadShadowReceiverFitInputs();
    float3 world_position;
    float view_depth;
    const bool receiver
        = TryLoadReceiverPixel(inputs, dispatch_id.xy, world_position, view_depth);

    [loop]
    for (uint cascade_index = 0u; cascade_index < inputs.cascade_count;
        ++cascade_index) {
        const VortexShadowCascadeBinding cascade
            = LoadReceiverCascade(inputs, cascade_index);
        const bool in_cascade
            = receiver && ReceiverSamplesCascade(inputs, cascade, view_depth);
        const float2 clip = in_cascade
            ? mul(cascade.light_view_projection, float4(world_position, 1.0f)).xy
            : 0.0f.xx;
        // One wave reduces its receivers to a single update.
        const float4 rect = float4(
            WaveActiveMin(in_cascade ? clip.x : SHADOW_RECEIVER_FLOAT_MAX),
            WaveActiveMin(in_cascade ? clip.y : SHADOW_RECEIVER_FLOAT_MAX),
            WaveActiveMax(in_cascade ? clip.x : -SHADOW_RECEIVER_FLOAT_MAX),
            WaveActiveMax(in_cascade ? clip.y : -SHADOW_RECEIVER_FLOAT_MAX));
        const uint count = WaveActiveCountBits(in_cascade);
        if (WaveIsFirstLane() && count != 0u) {
            AccumulateBounds(inputs, cascade_index, rect, count);
        }
    }
}

// One thread per draw; only translucent draws are receivers here.
[shader("compute")]
[numthreads(SHADOW_RECEIVER_LINEAR_GROUP_SIZE, 1, 1)]
void VortexShadowReceiverTranslucentCS(uint3 dispatch_id : SV_DispatchThreadID)
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
        if (ReceiverRangeSamplesCascade(
                inputs, cascade, depth_range.x, depth_range.y)) {
            AccumulateBounds(inputs, cascade_index,
                clamp(ReceiverClipRect(cascade.light_view_projection, corners),
                    -1.0f, 1.0f),
                1u);
        }
    }
}

// One thread per cascade. A cascade without receivers keeps its loose
// record; its receiver mask stays empty, so it draws no casters.
[shader("compute")]
[numthreads(SHADOW_RECEIVER_LINEAR_GROUP_SIZE, 1, 1)]
void VortexShadowReceiverFitCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const ShadowReceiverFitInputs inputs = LoadShadowReceiverFitInputs();
    if (dispatch_id.x >= inputs.cascade_count) {
        return;
    }
    RWStructuredBuffer<ShadowReceiverBounds> bounds_buffer
        = ResourceDescriptorHeap[inputs.bounds_uav];
    RWStructuredBuffer<VortexShadowCascadeBinding> cascades
        = ResourceDescriptorHeap[inputs.cascade_records_uav];
    const ShadowReceiverBounds bounds = bounds_buffer[dispatch_id.x];
    VortexShadowCascadeBinding cascade = cascades[dispatch_id.x];
    if (bounds.receiver_count == 0u || cascade.inverse_resolution.x <= 0.0f) {
        return;
    }

    // The loose cascade maps light-space x and y in [-radius, radius] to clip
    // [-1, 1]; receivers never leave it.
    const float resolution = 1.0f / cascade.inverse_resolution.x;
    const float radius = 0.5f * cascade.world_texel_size * resolution;
    const float4 rect = clamp(float4(OrderedToFloat(bounds.min_x),
                                  OrderedToFloat(bounds.min_y),
                                  OrderedToFloat(bounds.max_x),
                                  OrderedToFloat(bounds.max_y)),
        -1.0f, 1.0f);
    const float2 center = 0.5f * (rect.xy + rect.zw) * radius;
    const float2 half_size = 0.5f * (rect.zw - rect.xy) * radius;
    // The sampling margin is in fitted texels: solve
    // half = receivers + normal bias + margin_texels * 2 * half / resolution.
    const float margin_scale
        = 1.0f - 2.0f * SHADOW_RECEIVER_SAMPLING_TEXELS / resolution;
    const float needed = (max(half_size.x, half_size.y) + cascade.normal_bias_m)
        / max(margin_scale, 1.0e-3f);
    if (needed >= radius) {
        return;
    }
    const float steps = floor(SHADOW_RECEIVER_EXTENT_STEPS_PER_OCTAVE
        * log2(radius / max(needed, 1.0e-6f)));
    const float fitted
        = radius * exp2(-steps / SHADOW_RECEIVER_EXTENT_STEPS_PER_OCTAVE);
    const float texel = 2.0f * fitted / resolution;
    // Snap the center in light space anchored at the world origin, not at the
    // loose cascade center, which moves with the camera.
    const float2 anchored = cascade.light_space_origin + center;
    const float2 snapped
        = round(anchored / texel) * texel - cascade.light_space_origin;

    const float scale = radius / fitted;
    const float4x4 narrow = float4x4(
        scale, 0.0f, 0.0f, -snapped.x / fitted,
        0.0f, scale, 0.0f, -snapped.y / fitted,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    cascade.light_view_projection = mul(narrow, cascade.light_view_projection);
    cascade.world_texel_size = texel;
    cascades[dispatch_id.x] = cascade;
}

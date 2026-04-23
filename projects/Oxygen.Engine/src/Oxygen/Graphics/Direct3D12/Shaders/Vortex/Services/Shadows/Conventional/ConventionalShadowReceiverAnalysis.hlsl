//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Services/Shadows/Conventional/ConventionalShadowReceiverAnalysis.hlsli"
#include "Vortex/Services/Shadows/Conventional/ConventionalShadowReceiverAnalysisJob.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Draw/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_SCREEN_THREAD_GROUP_SIZE = 8u;
static const uint CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_JOB_THREAD_GROUP_SIZE = 64u;
static const float CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLOAT_MAX = 3.402823466e+38f;

struct ConventionalShadowReceiverAnalysisPassConstants
{
    uint depth_texture_index;
    uint job_buffer_index;
    uint raw_buffer_uav_index;
    uint raw_buffer_srv_index;
    uint analysis_buffer_uav_index;
    uint2 screen_dimensions;
    uint job_count;
    uint _pad0;
    uint3 _pad_after_job_count;
    float4x4 inverse_view_projection;
    float4x4 view_matrix;
};

struct ConventionalShadowReceiverAnalysisRaw
{
    uint min_x_ordered;
    uint min_y_ordered;
    uint max_x_ordered;
    uint max_y_ordered;
    uint min_z_ordered;
    uint max_z_ordered;
    uint sample_count;
    uint _pad0;
};

static uint FloatToOrdered(const float value)
{
    const uint bits = asuint(value);
    return (bits & 0x80000000u) != 0u ? ~bits : (bits ^ 0x80000000u);
}

static float OrderedToFloat(const uint ordered)
{
    const uint bits = (ordered & 0x80000000u) != 0u
        ? (ordered ^ 0x80000000u)
        : ~ordered;
    return asfloat(bits);
}

static float3 ReconstructWorldPosition(
    const uint2 pixel_coord,
    const float depth,
    const uint2 screen_dimensions,
    const float4x4 inverse_view_projection)
{
    const float2 uv
        = (float2(pixel_coord) + 0.5f) / float2(screen_dimensions);
    const float2 ndc_xy = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 clip = float4(ndc_xy, depth, 1.0f);
    const float4 world = mul(inverse_view_projection, clip);
    return world.xyz / max(world.w, 1.0e-6f);
}

static float ComputeEyeDepth(const float3 world_position_ws, const float4x4 view_matrix)
{
    const float4 view_position = mul(view_matrix, float4(world_position_ws, 1.0f));
    return max(0.0f, -view_position.z);
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_JOB_THREAD_GROUP_SIZE, 1, 1)]
void CS_Clear(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverAnalysisPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.job_count
        || !BX_IsValidSlot(pass_constants.raw_buffer_uav_index)) {
        return;
    }

    RWStructuredBuffer<ConventionalShadowReceiverAnalysisRaw> raw_buffer
        = ResourceDescriptorHeap[pass_constants.raw_buffer_uav_index];
    ConventionalShadowReceiverAnalysisRaw cleared;
    cleared.min_x_ordered = FloatToOrdered(CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLOAT_MAX);
    cleared.min_y_ordered = FloatToOrdered(CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLOAT_MAX);
    cleared.max_x_ordered = FloatToOrdered(-CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLOAT_MAX);
    cleared.max_y_ordered = FloatToOrdered(-CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLOAT_MAX);
    cleared.min_z_ordered = FloatToOrdered(CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLOAT_MAX);
    cleared.max_z_ordered = FloatToOrdered(-CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLOAT_MAX);
    cleared.sample_count = 0u;
    cleared._pad0 = 0u;
    raw_buffer[dispatch_thread_id.x] = cleared;
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_SCREEN_THREAD_GROUP_SIZE,
    CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_SCREEN_THREAD_GROUP_SIZE, 1)]
void CS_Analyze(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverAnalysisPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.screen_dimensions.x
        || dispatch_thread_id.y >= pass_constants.screen_dimensions.y
        || !BX_IsValidSlot(pass_constants.depth_texture_index)
        || !BX_IsValidSlot(pass_constants.job_buffer_index)
        || !BX_IsValidSlot(pass_constants.raw_buffer_uav_index)) {
        return;
    }

    Texture2D<float> depth_texture = ResourceDescriptorHeap[pass_constants.depth_texture_index];
    StructuredBuffer<ConventionalShadowReceiverAnalysisJob> jobs
        = ResourceDescriptorHeap[pass_constants.job_buffer_index];
    RWStructuredBuffer<ConventionalShadowReceiverAnalysisRaw> raw_buffer
        = ResourceDescriptorHeap[pass_constants.raw_buffer_uav_index];

    const float depth = depth_texture.Load(int3(dispatch_thread_id.xy, 0)).r;
    if (depth <= 0.0f) {
        return;
    }

    const float3 world_position_ws = ReconstructWorldPosition(dispatch_thread_id.xy,
        depth, pass_constants.screen_dimensions, pass_constants.inverse_view_projection);
    const float eye_depth = ComputeEyeDepth(world_position_ws, pass_constants.view_matrix);
    if (eye_depth <= 0.0f) {
        return;
    }

    [loop]
    for (uint job_index = 0u; job_index < pass_constants.job_count; ++job_index) {
        const ConventionalShadowReceiverAnalysisJob job = jobs[job_index];
        if ((job.flags & CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_VALID) == 0u) {
            continue;
        }
        const float split_begin = job.split_and_full_depth_range.x;
        const float split_end = job.split_and_full_depth_range.y;
        const bool in_split = eye_depth <= split_end
            && (job_index == 0u ? eye_depth >= split_begin : eye_depth > split_begin);
        if (!in_split) {
            continue;
        }

        const float3 light_space = mul(job.light_rotation_matrix, float4(world_position_ws, 1.0f)).xyz;
        InterlockedMin(raw_buffer[job_index].min_x_ordered, FloatToOrdered(light_space.x));
        InterlockedMin(raw_buffer[job_index].min_y_ordered, FloatToOrdered(light_space.y));
        InterlockedMax(raw_buffer[job_index].max_x_ordered, FloatToOrdered(light_space.x));
        InterlockedMax(raw_buffer[job_index].max_y_ordered, FloatToOrdered(light_space.y));
        InterlockedMin(raw_buffer[job_index].min_z_ordered, FloatToOrdered(light_space.z));
        InterlockedMax(raw_buffer[job_index].max_z_ordered, FloatToOrdered(light_space.z));
        InterlockedAdd(raw_buffer[job_index].sample_count, 1u);
    }
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_JOB_THREAD_GROUP_SIZE, 1, 1)]
void CS_Finalize(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverAnalysisPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.job_count
        || !BX_IsValidSlot(pass_constants.job_buffer_index)
        || !BX_IsValidSlot(pass_constants.raw_buffer_srv_index)
        || !BX_IsValidSlot(pass_constants.analysis_buffer_uav_index)) {
        return;
    }

    StructuredBuffer<ConventionalShadowReceiverAnalysisJob> jobs
        = ResourceDescriptorHeap[pass_constants.job_buffer_index];
    StructuredBuffer<ConventionalShadowReceiverAnalysisRaw> raw_buffer
        = ResourceDescriptorHeap[pass_constants.raw_buffer_srv_index];
    RWStructuredBuffer<ConventionalShadowReceiverAnalysis> analysis_buffer
        = ResourceDescriptorHeap[pass_constants.analysis_buffer_uav_index];

    const uint job_index = dispatch_thread_id.x;
    const ConventionalShadowReceiverAnalysisJob job = jobs[job_index];
    const ConventionalShadowReceiverAnalysisRaw raw = raw_buffer[job_index];

    const float world_units_per_texel = max(job.shading_margins.x, 0.0f);
    const float constant_bias = abs(job.shading_margins.y);
    const float normal_bias = abs(job.shading_margins.z);
    const float xy_dilation_margin = max(world_units_per_texel,
        constant_bias + normal_bias + 2.0f * world_units_per_texel);
    const float depth_dilation_margin = max(world_units_per_texel,
        constant_bias + normal_bias);

    ConventionalShadowReceiverAnalysis output;
    output.raw_xy_min_max = float4(0.0f, 0.0f, 0.0f, 0.0f);
    output.raw_depth_and_dilation
        = float4(0.0f, 0.0f, xy_dilation_margin, depth_dilation_margin);
    output.full_rect_center_half_extent = job.full_rect_center_half_extent;
    output.legacy_rect_center_half_extent = job.legacy_rect_center_half_extent;
    output.full_depth_and_area_ratios
        = float4(job.split_and_full_depth_range.z, job.split_and_full_depth_range.w, 0.0f, 0.0f);
    output.full_depth_ratio = 0.0f;
    output.sample_count = raw.sample_count;
    output.target_array_slice = job.target_array_slice;
    output.flags = raw.sample_count > 0u
        ? CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_VALID
        : CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_EMPTY;

    if (raw.sample_count > 0u) {
        const float min_x = OrderedToFloat(raw.min_x_ordered);
        const float min_y = OrderedToFloat(raw.min_y_ordered);
        const float max_x = OrderedToFloat(raw.max_x_ordered);
        const float max_y = OrderedToFloat(raw.max_y_ordered);
        const float min_z = OrderedToFloat(raw.min_z_ordered);
        const float max_z = OrderedToFloat(raw.max_z_ordered);

        output.raw_xy_min_max = float4(min_x, min_y, max_x, max_y);
        output.raw_depth_and_dilation = float4(
            min_z, max_z, xy_dilation_margin, depth_dilation_margin);

        const float sample_width = max(max_x - min_x, 0.0f);
        const float sample_height = max(max_y - min_y, 0.0f);
        const float sample_area = sample_width * sample_height;
        const float full_area = max(4.0f * job.full_rect_center_half_extent.z
                * job.full_rect_center_half_extent.w, 0.0f);
        const float legacy_area = max(4.0f * job.legacy_rect_center_half_extent.z
                * job.legacy_rect_center_half_extent.w, 0.0f);
        const float full_depth_span = max(
            job.split_and_full_depth_range.w - job.split_and_full_depth_range.z, 0.0f);
        const float sample_depth_span = max(max_z - min_z, 0.0f);

        output.full_depth_and_area_ratios.z = full_area > 0.0f
            ? (sample_area / full_area)
            : 0.0f;
        output.full_depth_and_area_ratios.w = legacy_area > 0.0f
            ? (sample_area / legacy_area)
            : 0.0f;
        output.full_depth_ratio = full_depth_span > 0.0f
            ? (sample_depth_span / full_depth_span)
            : 0.0f;
    }

    analysis_buffer[job_index] = output;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 12 instance culling and compact indirect draw emission.
//
// Each thread tests one draw bound against one prepared raster page. Visible
// draws append into a page-local indirect-command segment using a per-page GPU
// counter. Previous-frame screen HZB is optional and acts as an additional
// occlusion heuristic when available.

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmIndirectDrawCommand.hlsli"
#include "Renderer/Vsm/VsmRasterPageJob.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_INSTANCE_CULL_THREAD_GROUP_SIZE = 64u;
static const float VSM_OCCLUSION_EPSILON = 1.0e-3f;
static const uint VSM_RASTER_PAGE_JOB_FLAG_STATIC_ONLY = 1u << 0u;
static const uint DRAW_PRIMITIVE_FLAG_STATIC_SHADOW_CASTER = 1u << 0u;

struct VsmInstanceCullingPassConstants
{
    uint page_job_buffer_index;
    uint draw_metadata_index;
    uint draw_bounds_index;
    uint previous_frame_hzb_index;
    uint indirect_commands_uav_index;
    uint command_counts_uav_index;
    uint prepared_page_count;
    uint partition_begin;
    uint partition_count;
    uint draw_bounds_count;
    uint max_commands_per_page;
    uint previous_frame_hzb_width;
    uint previous_frame_hzb_height;
    uint previous_frame_hzb_mip_count;
    uint previous_frame_hzb_available;
    uint reveal_flags_index;
};

struct VsmClipSphere
{
    float4 center;
    float4 extent;
};

static VsmClipSphere VsmBuildClipSphere(
    const float4x4 view_projection_matrix, const float4 sphere_ws)
{
    const float3 center_ws = sphere_ws.xyz;
    const float radius = max(sphere_ws.w, 0.0f);

    const float4 center_clip
        = mul(view_projection_matrix, float4(center_ws, 1.0f));
    const float4 x_clip = mul(
        view_projection_matrix, float4(center_ws + float3(radius, 0.0f, 0.0f), 1.0f));
    const float4 y_clip = mul(
        view_projection_matrix, float4(center_ws + float3(0.0f, radius, 0.0f), 1.0f));
    const float4 z_clip = mul(
        view_projection_matrix, float4(center_ws + float3(0.0f, 0.0f, radius), 1.0f));

    VsmClipSphere result;
    result.center = center_clip;
    result.extent = abs(x_clip - center_clip);
    result.extent = max(result.extent, abs(y_clip - center_clip));
    result.extent = max(result.extent, abs(z_clip - center_clip));
    return result;
}

static bool VsmIntersectsD3DClip(const VsmClipSphere clip_sphere)
{
    const float max_w = clip_sphere.center.w + clip_sphere.extent.w;
    if (max_w <= 1.0e-5f) {
        return false;
    }

    if (clip_sphere.center.x + clip_sphere.extent.x < -max_w
        || clip_sphere.center.x - clip_sphere.extent.x > max_w) {
        return false;
    }
    if (clip_sphere.center.y + clip_sphere.extent.y < -max_w
        || clip_sphere.center.y - clip_sphere.extent.y > max_w) {
        return false;
    }
    if (clip_sphere.center.z + clip_sphere.extent.z < 0.0f
        || clip_sphere.center.z - clip_sphere.extent.z > max_w) {
        return false;
    }

    return true;
}

static bool VsmProjectClipSphereToHzb(const VsmClipSphere clip_sphere,
    const uint hzb_width, const uint hzb_height, out uint2 pixel_min,
    out uint2 pixel_max, out uint mip_level, out float front_depth)
{
    pixel_min = uint2(0u, 0u);
    pixel_max = uint2(0u, 0u);
    mip_level = 0u;
    front_depth = 0.0f;

    if (hzb_width == 0u || hzb_height == 0u) {
        return false;
    }

    const float near_w = clip_sphere.center.w - clip_sphere.extent.w;
    if (near_w <= 1.0e-5f) {
        return false;
    }

    const float2 ndc_min
        = (clip_sphere.center.xy - clip_sphere.extent.xy) / near_w;
    const float2 ndc_max
        = (clip_sphere.center.xy + clip_sphere.extent.xy) / near_w;

    if (ndc_max.x < -1.0f || ndc_min.x > 1.0f
        || ndc_max.y < -1.0f || ndc_min.y > 1.0f) {
        return false;
    }

    const float2 clamped_ndc_min = clamp(ndc_min, float2(-1.0f, -1.0f),
        float2(1.0f, 1.0f));
    const float2 clamped_ndc_max = clamp(ndc_max, float2(-1.0f, -1.0f),
        float2(1.0f, 1.0f));

    const float2 uv_min = float2(
        clamped_ndc_min.x * 0.5f + 0.5f,
        0.5f - clamped_ndc_max.y * 0.5f);
    const float2 uv_max = float2(
        clamped_ndc_max.x * 0.5f + 0.5f,
        0.5f - clamped_ndc_min.y * 0.5f);

    const float2 rect_texels = max(
        (uv_max - uv_min) * float2(hzb_width, hzb_height), float2(1.0f, 1.0f));
    const float max_extent = max(rect_texels.x, rect_texels.y);
    mip_level = (uint)floor(log2(max_extent));

    pixel_min = min((uint2)floor(uv_min * float2(hzb_width, hzb_height)),
        uint2(hzb_width - 1u, hzb_height - 1u));
    pixel_max = min((uint2)floor(
                        max(uv_max * float2(hzb_width, hzb_height) - 1.0f, 0.0f)),
        uint2(hzb_width - 1u, hzb_height - 1u));

    front_depth = saturate(
        (clip_sphere.center.z - clip_sphere.extent.z) / near_w);
    return true;
}

static bool VsmIsOccludedByPreviousHzb(const Texture2D<float> previous_frame_hzb,
    const VsmClipSphere camera_clip_sphere,
    const VsmInstanceCullingPassConstants pass_constants)
{
    if (pass_constants.previous_frame_hzb_available == 0u
        || !BX_IsValidSlot(pass_constants.previous_frame_hzb_index)
        || pass_constants.previous_frame_hzb_width == 0u
        || pass_constants.previous_frame_hzb_height == 0u
        || pass_constants.previous_frame_hzb_mip_count == 0u) {
        return false;
    }

    uint2 pixel_min = uint2(0u, 0u);
    uint2 pixel_max = uint2(0u, 0u);
    uint mip_level = 0u;
    float front_depth = 0.0f;
    if (!VsmProjectClipSphereToHzb(camera_clip_sphere,
            pass_constants.previous_frame_hzb_width,
            pass_constants.previous_frame_hzb_height, pixel_min, pixel_max,
            mip_level, front_depth)) {
        return false;
    }

    mip_level = min(
        mip_level, pass_constants.previous_frame_hzb_mip_count - 1u);

    const uint mip_width
        = max(pass_constants.previous_frame_hzb_width >> mip_level, 1u);
    const uint mip_height
        = max(pass_constants.previous_frame_hzb_height >> mip_level, 1u);

    const float2 scale = float2(
        (float)mip_width / (float)pass_constants.previous_frame_hzb_width,
        (float)mip_height / (float)pass_constants.previous_frame_hzb_height);
    const uint2 sample_min
        = min((uint2)floor(float2(pixel_min) * scale),
            uint2(mip_width - 1u, mip_height - 1u));
    const uint2 sample_max
        = min((uint2)floor(float2(pixel_max) * scale),
            uint2(mip_width - 1u, mip_height - 1u));
    const uint2 sample_center = min((sample_min + sample_max) / 2u,
        uint2(mip_width - 1u, mip_height - 1u));

    const float depth00 = previous_frame_hzb.Load(int3(sample_min, mip_level)).r;
    const float depth10 = previous_frame_hzb.Load(
        int3(uint2(sample_max.x, sample_min.y), mip_level)).r;
    const float depth01 = previous_frame_hzb.Load(
        int3(uint2(sample_min.x, sample_max.y), mip_level)).r;
    const float depth11 = previous_frame_hzb.Load(int3(sample_max, mip_level)).r;
    const float depth_center
        = previous_frame_hzb.Load(int3(sample_center, mip_level)).r;

    return front_depth > depth00 + VSM_OCCLUSION_EPSILON
        && front_depth > depth10 + VSM_OCCLUSION_EPSILON
        && front_depth > depth01 + VSM_OCCLUSION_EPSILON
        && front_depth > depth11 + VSM_OCCLUSION_EPSILON
        && front_depth > depth_center + VSM_OCCLUSION_EPSILON;
}

[shader("compute")]
[numthreads(VSM_INSTANCE_CULL_THREAD_GROUP_SIZE, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmInstanceCullingPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.partition_count
        || dispatch_thread_id.y >= pass_constants.prepared_page_count
        || pass_constants.partition_count == 0u
        || pass_constants.prepared_page_count == 0u
        || pass_constants.max_commands_per_page == 0u) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.page_job_buffer_index)
        || !BX_IsValidSlot(pass_constants.draw_metadata_index)
        || !BX_IsValidSlot(pass_constants.draw_bounds_index)
        || !BX_IsValidSlot(pass_constants.indirect_commands_uav_index)
        || !BX_IsValidSlot(pass_constants.command_counts_uav_index)) {
        return;
    }

    const uint draw_index = pass_constants.partition_begin + dispatch_thread_id.x;
    if (draw_index >= pass_constants.draw_bounds_count) {
        return;
    }

    StructuredBuffer<VsmRasterPageJob> page_jobs
        = ResourceDescriptorHeap[pass_constants.page_job_buffer_index];
    StructuredBuffer<DrawMetadata> draw_metadata
        = ResourceDescriptorHeap[pass_constants.draw_metadata_index];
    StructuredBuffer<float4> draw_bounds
        = ResourceDescriptorHeap[pass_constants.draw_bounds_index];
    RWStructuredBuffer<VsmIndirectDrawCommand> indirect_commands
        = ResourceDescriptorHeap[pass_constants.indirect_commands_uav_index];
    RWStructuredBuffer<uint> command_counts
        = ResourceDescriptorHeap[pass_constants.command_counts_uav_index];

    const DrawMetadata metadata = draw_metadata[draw_index];
    const uint vertex_count_per_instance
        = metadata.is_indexed != 0u ? metadata.index_count : metadata.vertex_count;
    if (metadata.instance_count == 0u || vertex_count_per_instance == 0u) {
        return;
    }

    const float4 sphere_ws = draw_bounds[draw_index];
    if (sphere_ws.w <= 0.0f) {
        return;
    }

    const VsmRasterPageJob page_job = page_jobs[dispatch_thread_id.y];
    if ((page_job.job_flags & VSM_RASTER_PAGE_JOB_FLAG_STATIC_ONLY) != 0u
        && (metadata.primitive_flags & DRAW_PRIMITIVE_FLAG_STATIC_SHADOW_CASTER) == 0u) {
        return;
    }

    uint reveal_flag = 0u;
    if (BX_IsValidSlot(pass_constants.reveal_flags_index)) {
        StructuredBuffer<uint> reveal_flags
            = ResourceDescriptorHeap[pass_constants.reveal_flags_index];
        reveal_flag = reveal_flags[draw_index];
    }
    const bool reveal_forced = reveal_flag != 0u;
    const VsmClipSphere page_clip_sphere
        = VsmBuildClipSphere(page_job.view_projection_matrix, sphere_ws);
    if (!VsmIntersectsD3DClip(page_clip_sphere)) {
        return;
    }

    bool occluded = false;
    if (!reveal_forced && pass_constants.previous_frame_hzb_available != 0u
        && BX_IsValidSlot(pass_constants.previous_frame_hzb_index)) {
        Texture2D<float> previous_frame_hzb
            = ResourceDescriptorHeap[pass_constants.previous_frame_hzb_index];
        const float4x4 camera_view_projection
            = mul(projection_matrix, view_matrix);
        const VsmClipSphere camera_clip_sphere
            = VsmBuildClipSphere(camera_view_projection, sphere_ws);
        occluded = VsmIsOccludedByPreviousHzb(
            previous_frame_hzb, camera_clip_sphere, pass_constants);
    }

    if (occluded) {
        return;
    }

    uint command_slot = 0u;
    InterlockedAdd(command_counts[dispatch_thread_id.y], 1u, command_slot);
    if (command_slot >= pass_constants.max_commands_per_page) {
        return;
    }

    const uint command_index
        = dispatch_thread_id.y * pass_constants.max_commands_per_page
        + command_slot;
    VsmIndirectDrawCommand command;
    command.draw_index = draw_index;
    command.vertex_count_per_instance = vertex_count_per_instance;
    command.instance_count = metadata.instance_count;
    command.start_vertex_location = 0u;
    command.start_instance_location = 0u;
    indirect_commands[command_index] = command;
}

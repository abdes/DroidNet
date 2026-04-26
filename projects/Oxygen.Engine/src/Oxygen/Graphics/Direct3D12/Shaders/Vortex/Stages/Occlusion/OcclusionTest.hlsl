//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Vortex Stage 5 HZB occlusion test.
//
// Consumes the current furthest screen HZB and writes one conservative
// visibility result per submitted draw candidate. CPU readback consumes these
// results on a later frame; missing or inconclusive GPU data remains visible.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Scene/ScreenHzbBindings.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VORTEX_OCCLUSION_THREAD_GROUP_SIZE = 64u;
static const float VORTEX_OCCLUSION_HZB_EPSILON = 1.0e-3f;

struct OcclusionCandidate
{
    float4 sphere_world;
    uint draw_index;
    uint3 _pad0;
};

struct OcclusionPassConstants
{
    uint candidates_srv;
    uint result_uav;
    uint screen_hzb_frame_slot;
    uint candidate_count;
};

struct OcclusionClipSphere
{
    float4 center;
    float4 extent;
};

static inline OcclusionClipSphere BuildOcclusionClipSphere(float4 sphere_world)
{
    const float3 center_ws = sphere_world.xyz;
    const float radius = max(sphere_world.w, 0.0f);

    const float4 center_clip
        = mul(projection_matrix, mul(view_matrix, float4(center_ws, 1.0f)));
    const float4 x_clip = mul(
        projection_matrix,
        mul(view_matrix, float4(center_ws + float3(radius, 0.0f, 0.0f), 1.0f)));
    const float4 y_clip = mul(
        projection_matrix,
        mul(view_matrix, float4(center_ws + float3(0.0f, radius, 0.0f), 1.0f)));
    const float4 z_clip = mul(
        projection_matrix,
        mul(view_matrix, float4(center_ws + float3(0.0f, 0.0f, radius), 1.0f)));

    OcclusionClipSphere result;
    result.center = center_clip;
    result.extent = abs(x_clip - center_clip);
    result.extent = max(result.extent, abs(y_clip - center_clip));
    result.extent = max(result.extent, abs(z_clip - center_clip));
    return result;
}

static inline float ResolveOcclusionNearestDepth(OcclusionClipSphere clip_sphere)
{
    const float near_w = clip_sphere.center.w - clip_sphere.extent.w;
    if (near_w <= 1.0e-5f)
    {
        return 0.0f;
    }

    return saturate((clip_sphere.center.z + clip_sphere.extent.z) / near_w);
}

static inline bool ProjectOcclusionClipSphereToViewportUvBounds(
    OcclusionClipSphere clip_sphere, out float2 uv_min, out float2 uv_max)
{
    uv_min = 0.0f.xx;
    uv_max = 0.0f.xx;

    const float near_w = clip_sphere.center.w - clip_sphere.extent.w;
    if (near_w <= 1.0e-5f)
    {
        uv_min = 0.0f.xx;
        uv_max = 1.0f.xx;
        return true;
    }

    const float2 ndc_min
        = (clip_sphere.center.xy - clip_sphere.extent.xy) / near_w;
    const float2 ndc_max
        = (clip_sphere.center.xy + clip_sphere.extent.xy) / near_w;

    if (ndc_max.x < -1.0f || ndc_min.x > 1.0f
        || ndc_max.y < -1.0f || ndc_min.y > 1.0f)
    {
        return false;
    }

    const float2 clamped_ndc_min = clamp(
        ndc_min, float2(-1.0f, -1.0f), float2(1.0f, 1.0f));
    const float2 clamped_ndc_max = clamp(
        ndc_max, float2(-1.0f, -1.0f), float2(1.0f, 1.0f));
    uv_min = float2(
        clamped_ndc_min.x * 0.5f + 0.5f,
        0.5f - clamped_ndc_max.y * 0.5f);
    uv_max = float2(
        clamped_ndc_max.x * 0.5f + 0.5f,
        0.5f - clamped_ndc_min.y * 0.5f);
    return true;
}

static inline bool ProjectOcclusionClipSphereToHzb(
    OcclusionClipSphere clip_sphere,
    ScreenHzbFrameBindingsData screen_hzb,
    out uint2 pixel_min,
    out uint2 pixel_max,
    out uint mip_level)
{
    pixel_min = uint2(0u, 0u);
    pixel_max = uint2(0u, 0u);
    mip_level = 0u;

    if (screen_hzb.width == 0u || screen_hzb.height == 0u)
    {
        return false;
    }

    float2 uv_min = 0.0f.xx;
    float2 uv_max = 0.0f.xx;
    if (!ProjectOcclusionClipSphereToViewportUvBounds(clip_sphere, uv_min, uv_max))
    {
        return false;
    }
    const float2 clamped_uv_min = clamp(uv_min, 0.0f.xx, 1.0f.xx);
    const float2 clamped_uv_max = clamp(uv_max, 0.0f.xx, 1.0f.xx);
    if (clamped_uv_max.x <= clamped_uv_min.x || clamped_uv_max.y <= clamped_uv_min.y)
    {
        return false;
    }

    const float2 hzb_uv_min
        = clamped_uv_min * GetViewportUvToHzbBufferUv(screen_hzb);
    const float2 hzb_uv_max
        = clamped_uv_max * GetViewportUvToHzbBufferUv(screen_hzb);
    const float2 rect_texels = max(
        (hzb_uv_max - hzb_uv_min) * GetHzbSize(screen_hzb),
        float2(1.0f, 1.0f));
    mip_level = (uint)floor(log2(max(rect_texels.x, rect_texels.y)));

    pixel_min = min((uint2)floor(hzb_uv_min * GetHzbSize(screen_hzb)),
        uint2(screen_hzb.width - 1u, screen_hzb.height - 1u));
    pixel_max = min((uint2)floor(
                        max(hzb_uv_max * GetHzbSize(screen_hzb) - 1.0f, 0.0f)),
        uint2(screen_hzb.width - 1u, screen_hzb.height - 1u));
    return true;
}

static inline bool IsOccludedByHzb(Texture2D<float> furthest_hzb,
    ScreenHzbFrameBindingsData screen_hzb,
    OcclusionClipSphere clip_sphere)
{
    if (!IsScreenHzbFurthestValid(screen_hzb)
        || screen_hzb.furthest_srv == K_INVALID_BINDLESS_INDEX
        || screen_hzb.width == 0u
        || screen_hzb.height == 0u
        || screen_hzb.mip_count == 0u)
    {
        return false;
    }

    uint2 pixel_min = uint2(0u, 0u);
    uint2 pixel_max = uint2(0u, 0u);
    uint mip_level = 0u;
    if (!ProjectOcclusionClipSphereToHzb(
            clip_sphere, screen_hzb, pixel_min, pixel_max, mip_level))
    {
        return false;
    }

    const float nearest_depth = ResolveOcclusionNearestDepth(clip_sphere);
    mip_level = min(mip_level, screen_hzb.mip_count - 1u);

    const uint mip_width = max(screen_hzb.width >> mip_level, 1u);
    const uint mip_height = max(screen_hzb.height >> mip_level, 1u);
    const float2 scale = float2(
        (float)mip_width / (float)screen_hzb.width,
        (float)mip_height / (float)screen_hzb.height);
    const uint2 sample_min = min((uint2)floor(float2(pixel_min) * scale),
        uint2(mip_width - 1u, mip_height - 1u));
    const uint2 sample_max = min((uint2)floor(float2(pixel_max) * scale),
        uint2(mip_width - 1u, mip_height - 1u));
    const uint2 sample_center = min((sample_min + sample_max) / 2u,
        uint2(mip_width - 1u, mip_height - 1u));

    const float depth00 = furthest_hzb.Load(int3(sample_min, mip_level)).r;
    const float depth10 = furthest_hzb.Load(
        int3(uint2(sample_max.x, sample_min.y), mip_level)).r;
    const float depth01 = furthest_hzb.Load(
        int3(uint2(sample_min.x, sample_max.y), mip_level)).r;
    const float depth11 = furthest_hzb.Load(int3(sample_max, mip_level)).r;
    const float depth_center = furthest_hzb.Load(int3(sample_center, mip_level)).r;

    return nearest_depth + VORTEX_OCCLUSION_HZB_EPSILON < depth00
        && nearest_depth + VORTEX_OCCLUSION_HZB_EPSILON < depth10
        && nearest_depth + VORTEX_OCCLUSION_HZB_EPSILON < depth01
        && nearest_depth + VORTEX_OCCLUSION_HZB_EPSILON < depth11
        && nearest_depth + VORTEX_OCCLUSION_HZB_EPSILON < depth_center;
}

[shader("compute")]
[numthreads(VORTEX_OCCLUSION_THREAD_GROUP_SIZE, 1, 1)]
void VortexOcclusionTestCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    ConstantBuffer<OcclusionPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.candidate_count
        || pass_constants.candidates_srv == K_INVALID_BINDLESS_INDEX
        || pass_constants.result_uav == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    StructuredBuffer<OcclusionCandidate> candidates
        = ResourceDescriptorHeap[pass_constants.candidates_srv];
    RWStructuredBuffer<uint> results
        = ResourceDescriptorHeap[pass_constants.result_uav];

    const OcclusionCandidate candidate = candidates[dispatch_thread_id.x];
    uint visible = 1u;
    const ScreenHzbFrameBindingsData screen_hzb
        = LoadScreenHzbBindings(pass_constants.screen_hzb_frame_slot);
    if (IsScreenHzbFurthestValid(screen_hzb))
    {
        Texture2D<float> furthest_hzb = ResourceDescriptorHeap[screen_hzb.furthest_srv];
        const OcclusionClipSphere clip_sphere
            = BuildOcclusionClipSphere(candidate.sphere_world);
        visible = IsOccludedByHzb(furthest_hzb, screen_hzb, clip_sphere) ? 0u : 1u;
    }

    results[dispatch_thread_id.x] = visible;
}

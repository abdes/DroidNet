//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_CONTACT_SHADOW_HLSLI
#define OXYGEN_VORTEX_CONTACT_SHADOW_HLSLI

#include "Vortex/Contracts/Shadows/ShadowFrameBindings.hlsli"

static const uint LIGHT_CONTACT_SHADOW_FLAGS = 3u; // Cast Shadows AND Contact Shadows.

static bool ProjectContactSample(float3 view_position, float4x4 projection, out float2 uv)
{
    const float4 clip = mul(projection, float4(view_position, 1.0f));
    uv = 0.0f.xx;
    if (clip.w <= 0.0f || clip.z < 0.0f || clip.z > clip.w) return false;
    uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
    return all(uv >= 0.0f) && all(uv < 1.0f);
}

static float ContactLinearDepth(float device_depth, float4x4 projection)
{
    // Invert clip.z/clip.w for either perspective or orthographic projection.
    return (device_depth * projection[3][3] - projection[2][3])
        / (device_depth * projection[3][2] - projection[2][2]);
}

static float TraceContactShadow(VortexShadowFrameBindings bindings,
    float4x4 view, float4x4 projection, bool reversed_depth,
    float3 world_position, float3 geometric_normal, float3 direction_to_light)
{
    const float3 origin = mul(view,
        float4(world_position + 0.001f * geometric_normal, 1.0f)).xyz;
    const float3 ray = mul((float3x3)view, direction_to_light);
    float2 start_uv;
    if (!ProjectContactSample(origin, projection, start_uv)) return 1.0f;
    Texture2D<float> depth = ResourceDescriptorHeap[bindings.contact_depth_srv];
    const int2 start_pixel = int2(bindings.contact_content_origin_px
        + start_uv * bindings.contact_content_extent_px);
    const float start_depth = depth.Load(int3(start_pixel, 0));

    [loop] for (uint i = 0u; i < 16u; ++i) {
        const float distance = 0.25f * float(i + 1u) / 16.0f;
        const float3 sample_position = origin + distance * ray;
        float2 uv;
        if (!ProjectContactSample(sample_position, projection, uv)) break;
        const int2 pixel = int2(bindings.contact_content_origin_px
            + uv * bindings.contact_content_extent_px);
        const float stored = depth.Load(int3(pixel, 0));
        if (!isfinite(stored) || (reversed_depth ? stored <= 0.0f : stored >= 1.0f)
            || asuint(stored) == asuint(start_depth)) continue;
        const float delta = -sample_position.z - ContactLinearDepth(stored, projection);
        if (delta > 0.0f && delta <= 0.002f) {
            const float2 edge_pixels = min(uv, 1.0f - uv)
                * bindings.contact_content_extent_px;
            const float edge_weight = saturate(min(edge_pixels.x, edge_pixels.y) / 8.0f);
            const float end_weight = 1.0f - smoothstep(0.20f, 0.25f, distance);
            return 1.0f - edge_weight * end_weight;
        }
    }
    return 1.0f;
}

static float ComputeContactShadowVisibility(uint light_flags, bool receives_shadows,
    float3 world_position, float3 geometric_normal, float3 direction_to_light)
{
    if (!receives_shadows
        || (light_flags & LIGHT_CONTACT_SHADOW_FLAGS) != LIGHT_CONTACT_SHADOW_FLAGS)
        return 1.0f;
    const VortexShadowFrameBindings bindings = LoadVortexShadowFrameBindings();
    // Required-product admission rejects this view before direct-light recording.
    if (bindings.contact_enabled == 0u || !BX_IN_TEXTURES(bindings.contact_depth_srv))
        return 1.0f;
    return TraceContactShadow(bindings, view_matrix, projection_matrix, reverse_z != 0u,
        world_position, geometric_normal, direction_to_light);
}

#endif

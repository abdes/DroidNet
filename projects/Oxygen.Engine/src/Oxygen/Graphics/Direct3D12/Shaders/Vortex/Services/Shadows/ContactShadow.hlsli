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

// Rasterized device depth is interpolated, not exact: allow 8 ULPs of 1.0.
static const float kDeviceDepthError = 8.0f / 8388608.0f;

// The linear depth a stored device depth can be off by. The error is absolute
// in device depth, so in linear depth it grows with d(linear)/d(device), which
// for a perspective projection is about depth^2 / near: tens of metres from a
// near plane of a few centimetres it reaches centimetres.
static float ContactDepthResolution(float device_depth, float4x4 projection)
{
    const float denominator =
        device_depth * projection[3][2] - projection[2][2];
    const float slope = (projection[2][3] * projection[3][2]
        - projection[3][3] * projection[2][2]) / (denominator * denominator);
    return abs(slope) * kDeviceDepthError;
}

// Marches 16 steps of a 0.25 m ray toward the light over the contact depth.
// A sample hits when it has passed behind the stored surface by less than
// twice the per-step advance, so occluders between samples are not skipped.
// Point-sampled depth of the receiver's own neighbouring pixels differs from
// the ray by up to half a pixel of depth slope. Reconstructed along the ray,
// such a point lies within half a pixel times sin(view-normal angle) of the
// receiver plane; samples that close to it are the receiver, not occluders.
// Depth-buffer error moves both the stored point and the reconstructed receiver
// along their eye rays; where the view looks along the normal the slope bound
// vanishes and that error alone decides, so it widens the bound too.
static float TraceContactShadow(VortexShadowFrameBindings bindings,
    float4x4 view, float4x4 projection, bool reversed_depth,
    float3 world_position, float3 geometric_normal, float3 direction_to_light)
{
    static const uint kSteps = 16u;
    static const float kRayLength = 0.25f;
    static const float kStepLength = kRayLength / float(kSteps);
    static const float kThickness = 2.0f * kStepLength;
    static const float kSurfaceOffset = 0.001f;

    const float3 surface = mul(view, float4(world_position, 1.0f)).xyz;
    const float3 normal = normalize(mul((float3x3)view, geometric_normal));
    const float3 origin = surface + kSurfaceOffset * normal;
    const float3 ray = mul((float3x3)view, direction_to_light);
    float2 start_uv;
    if (!ProjectContactSample(origin, projection, start_uv)) return 1.0f;
    Texture2D<float> depth = ResourceDescriptorHeap[bindings.contact_depth_srv];
    const int2 start_pixel = int2(bindings.contact_content_origin_px
        + start_uv * bindings.contact_content_extent_px);
    const float start_depth = depth.Load(int3(start_pixel, 0));
    // World size of one pixel per unit of view depth (perspective) or in
    // absolute terms (orthographic).
    const bool perspective = projection[3][3] == 0.0f;
    const float pixel_scale =
        2.0f / (abs(projection[1][1]) * bindings.contact_content_extent_px.y);
    const float3 view_ray = perspective ? normalize(surface) : float3(0.0f, 0.0f, -1.0f);
    const float view_sine = sqrt(saturate(1.0f - dot(normal, view_ray) * dot(normal, view_ray)));

    [loop] for (uint i = 0u; i < kSteps; ++i) {
        const float distance = kStepLength * float(i + 1u);
        const float3 sample_position = origin + distance * ray;
        float2 uv;
        if (!ProjectContactSample(sample_position, projection, uv)) break;
        const int2 pixel = int2(bindings.contact_content_origin_px
            + uv * bindings.contact_content_extent_px);
        const float stored = depth.Load(int3(pixel, 0));
        if (!isfinite(stored) || (reversed_depth ? stored <= 0.0f : stored >= 1.0f)
            || asuint(stored) == asuint(start_depth)) continue;
        const float sample_depth = -sample_position.z;
        const float stored_depth = ContactLinearDepth(stored, projection);
        const float delta = sample_depth - stored_depth;
        if (delta <= 0.0f || delta > kThickness) continue;
        // The stored surface point along this sample's eye ray.
        const float3 stored_point = perspective
            ? sample_position * (stored_depth / sample_depth)
            : float3(sample_position.xy, -stored_depth);
        // Plane distance per unit of linear depth along the sample's eye ray.
        const float3 sample_ray = perspective
            ? normalize(sample_position) : float3(0.0f, 0.0f, -1.0f);
        const float depth_to_plane =
            abs(dot(normal, sample_ray)) / max(abs(sample_ray.z), 1.0e-4f);
        // One pixel of margin over the half-pixel bound, and the depth error
        // of both the stored sample and the receiver.
        const float footprint = pixel_scale * (perspective ? stored_depth : 1.0f)
            * view_sine + kSurfaceOffset
            + 2.0f * ContactDepthResolution(stored, projection) * depth_to_plane;
        if (abs(dot(normal, stored_point - surface)) <= footprint) continue;
        const float2 edge_pixels = min(uv, 1.0f - uv)
            * bindings.contact_content_extent_px;
        const float edge_weight = saturate(min(edge_pixels.x, edge_pixels.y) / 8.0f);
        const float end_weight = 1.0f - smoothstep(0.20f, 0.25f, distance);
        return 1.0f - edge_weight * end_weight;
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

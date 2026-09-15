//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SERVICES_SHADOWS_SHADOWSURFACENORMAL_HLSLI
#define OXYGEN_VORTEX_SERVICES_SHADOWS_SHADOWSURFACENORMAL_HLSLI

// Pixel-shader geometric normal for receiver offsets, independent of normal maps.
static inline float3 ComputeShadowSurfaceNormal(
    float3 world_pos,
    float3 world_normal,
    bool is_front_face)
{
    float3 fallback_shadow_normal = is_front_face ? world_normal : -world_normal;
    const float fallback_shadow_normal_len_sq =
        dot(fallback_shadow_normal, fallback_shadow_normal);
    fallback_shadow_normal = fallback_shadow_normal_len_sq > 1.0e-8
        ? fallback_shadow_normal * rsqrt(fallback_shadow_normal_len_sq)
        : float3(0.0, 0.0, 1.0);
    const float3 world_pos_ddx = ddx_fine(world_pos);
    const float3 world_pos_ddy = ddy_fine(world_pos);
    float3 shadow_normal = cross(world_pos_ddx, world_pos_ddy);
    const float shadow_normal_len_sq = dot(shadow_normal, shadow_normal);
    shadow_normal = shadow_normal_len_sq > 1.0e-8
        ? normalize(shadow_normal)
        : float3(0.0, 0.0, 0.0);
    if (dot(shadow_normal, fallback_shadow_normal) < 0.0) {
        shadow_normal = -shadow_normal;
    }
    if (dot(fallback_shadow_normal, shadow_normal) < 0.25) {
        shadow_normal = fallback_shadow_normal;
    }
    return shadow_normal;
}

#endif // OXYGEN_VORTEX_SERVICES_SHADOWS_SHADOWSURFACENORMAL_HLSLI

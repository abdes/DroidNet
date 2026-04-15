//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_POSITIONRECONSTRUCTION_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_POSITIONRECONSTRUCTION_HLSLI

static inline float4 MakeClipPositionFromScreenUv(float2 screen_uv, float device_depth)
{
    const float2 ndc_xy = float2(
        mad(screen_uv.x, 2.0f, -1.0f),
        mad(1.0f - screen_uv.y, 2.0f, -1.0f));
    return float4(ndc_xy, device_depth, 1.0f);
}

static inline float3 ReconstructViewPosition(
    float2 screen_uv, float device_depth, float4x4 inverse_projection_matrix)
{
    const float4 clip_position
        = MakeClipPositionFromScreenUv(screen_uv, device_depth);
    const float4 view_position = mul(inverse_projection_matrix, clip_position);
    const float inv_w = abs(view_position.w) > 1e-6f ? rcp(view_position.w) : 0.0f;
    return view_position.xyz * inv_w;
}

static inline float3 ReconstructWorldPosition(float2 screen_uv, float device_depth,
    float4x4 inverse_view_projection_matrix)
{
    const float4 clip_position
        = MakeClipPositionFromScreenUv(screen_uv, device_depth);
    const float4 world_position
        = mul(inverse_view_projection_matrix, clip_position);
    const float inv_w = abs(world_position.w) > 1e-6f ? rcp(world_position.w) : 0.0f;
    return world_position.xyz * inv_w;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_POSITIONRECONSTRUCTION_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Clustered light-grid build for Forward+ rendering.
//
// This shader implements one shipping path only:
// - analytic XY frustum subdivision
// - UE-style `LightGridZParams = (B, O, S)` Z slicing
// - no scene depth or HZB dependency
// - flat `uint2(offset, count)` grid + flat light-index list outputs

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli" // Required by BindlessHelpers.hlsl.
#include "Renderer/MaterialShadingConstants.hlsli" // Required by BindlessHelpers.hlsl.
#include "Renderer/PositionalLightData.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint LIGHT_GRID_THREADGROUP_SIZE = 4u;

struct LightCullingPassConstants
{
    uint light_buffer_index;
    uint light_list_uav_index;
    uint light_count_uav_index;
    uint _pad0;

    float4x4 inv_projection_matrix;
    float2 screen_dimensions;
    uint num_lights;
    uint _pad1;

    uint cluster_dim_x;
    uint cluster_dim_y;
    uint cluster_dim_z;
    uint light_grid_pixel_size_shift;

    float light_grid_z_params_b;
    float light_grid_z_params_o;
    float light_grid_z_params_s;
    uint max_lights_per_cell;
};

float3 SafeNormalize3(float3 value)
{
    const float len_sq = dot(value, value);
    if (len_sq <= 1.0e-8f) {
        return 0.0.xxx;
    }
    return value * rsqrt(len_sq);
}

float DeviceDepthFromLinearViewDepth(float linear_depth)
{
    const float4 clip = mul(projection_matrix, float4(0.0, 0.0, -linear_depth, 1.0));
    return clip.z / max(clip.w, 1.0e-6);
}

float3 ClipToView(float2 clip_xy, float device_depth, float4x4 inv_projection_matrix)
{
    const float4 view = mul(inv_projection_matrix, float4(clip_xy, device_depth, 1.0));
    return view.xyz / max(view.w, 1.0e-6f);
}

float ComputeCellNearViewDepthFromZSlice(
    uint z_slice,
    float3 light_grid_z_params,
    uint cluster_dim_z)
{
    float slice_depth
        = (exp2(z_slice / light_grid_z_params.z) - light_grid_z_params.y)
        / light_grid_z_params.x;

    if (z_slice == cluster_dim_z) {
        slice_depth = 2000000.0f;
    }

    if (z_slice == 0u) {
        slice_depth = 0.0f;
    }

    return slice_depth;
}

void ComputeCellViewAabb(
    uint3 cluster_id,
    float2 screen_dimensions,
    float4x4 inv_projection_matrix,
    uint light_grid_pixel_size_shift,
    float3 light_grid_z_params,
    uint cluster_dim_z,
    out float3 view_tile_min,
    out float3 view_tile_max)
{
    const float2 inv_grid_size
        = (1u << light_grid_pixel_size_shift) / screen_dimensions;
    const float2 tile_size = float2(2.0f, -2.0f) * inv_grid_size;
    const float2 clip_origin = float2(-1.0f, 1.0f);

    const float2 clip_tile_min = cluster_id.xy * tile_size + clip_origin;
    const float2 clip_tile_max = (cluster_id.xy + 1u) * tile_size + clip_origin;

    const float min_tile_z = ComputeCellNearViewDepthFromZSlice(
        cluster_id.z, light_grid_z_params, cluster_dim_z);
    const float max_tile_z = ComputeCellNearViewDepthFromZSlice(
        cluster_id.z + 1u, light_grid_z_params, cluster_dim_z);

    const float min_tile_device_z = DeviceDepthFromLinearViewDepth(min_tile_z);
    const float max_tile_device_z = DeviceDepthFromLinearViewDepth(max_tile_z);

    const float3 corners[8] = {
        ClipToView(float2(clip_tile_min.x, clip_tile_min.y), min_tile_device_z, inv_projection_matrix),
        ClipToView(float2(clip_tile_max.x, clip_tile_min.y), min_tile_device_z, inv_projection_matrix),
        ClipToView(float2(clip_tile_min.x, clip_tile_max.y), min_tile_device_z, inv_projection_matrix),
        ClipToView(float2(clip_tile_max.x, clip_tile_max.y), min_tile_device_z, inv_projection_matrix),
        ClipToView(float2(clip_tile_min.x, clip_tile_min.y), max_tile_device_z, inv_projection_matrix),
        ClipToView(float2(clip_tile_max.x, clip_tile_min.y), max_tile_device_z, inv_projection_matrix),
        ClipToView(float2(clip_tile_min.x, clip_tile_max.y), max_tile_device_z, inv_projection_matrix),
        ClipToView(float2(clip_tile_max.x, clip_tile_max.y), max_tile_device_z, inv_projection_matrix)
    };

    view_tile_min = corners[0];
    view_tile_max = corners[0];
    [unroll]
    for (uint i = 1u; i < 8u; ++i) {
        view_tile_min = min(view_tile_min, corners[i]);
        view_tile_max = max(view_tile_max, corners[i]);
    }
}

float ComputeSquaredDistanceFromBoxToPoint(
    float3 center,
    float3 extents,
    float3 probe_position)
{
    const float3 delta = abs(probe_position - center) - extents;
    const float3 clamped = max(delta, 0.0.xxx);
    return dot(clamped, clamped);
}

bool AabbOutsidePlane(float3 center, float3 extents, float4 plane)
{
    const float dist = dot(float4(center, 1.0f), plane);
    const float radius = dot(extents, abs(plane.xyz));
    return dist > radius;
}

bool IsAabbOutsideInfiniteAcuteConeApprox(
    float3 cone_vertex,
    float3 cone_axis,
    float tan_cone_angle,
    float3 aabb_center,
    float3 aabb_extents)
{
    const float3 d = aabb_center - cone_vertex;
    const float3 cross_axis = cross(d, cone_axis);
    const float3 m = -SafeNormalize3(cross(cross_axis, cone_axis));
    const float3 n = -tan_cone_angle * cone_axis + m;
    return AabbOutsidePlane(d, aabb_extents, float4(n, 0.0f));
}

uint ComputeLinearClusterIndex(uint3 cluster_id, uint3 cluster_dims)
{
    return (cluster_id.z * cluster_dims.y + cluster_id.y) * cluster_dims.x
         + cluster_id.x;
}

bool TestLightAgainstCell(
    PositionalLightData light,
    float3 view_tile_center,
    float3 view_tile_extent)
{
    if ((light.flags & POSITIONAL_LIGHT_FLAG_AFFECTS_WORLD) == 0u) {
        return false;
    }

    const float3 view_space_light_position
        = mul(view_matrix, float4(light.position_ws, 1.0f)).xyz;
    const float light_radius = max(light.range, 0.0f);
    const float box_distance_sq = ComputeSquaredDistanceFromBoxToPoint(
        view_tile_center, view_tile_extent, view_space_light_position);
    if (box_distance_sq >= light_radius * light_radius) {
        return false;
    }

    if ((light.flags & POSITIONAL_LIGHT_TYPE_MASK) == POSITIONAL_LIGHT_TYPE_SPOT
        && light.outer_cone_cos > 0.0f) {
        const float cone_sin = sqrt(saturate(1.0f - light.outer_cone_cos * light.outer_cone_cos));
        const float tan_cone_angle = cone_sin / max(light.outer_cone_cos, 1.0e-4f);
        const float3 view_space_light_direction = -SafeNormalize3(
            mul(view_matrix, float4(light.direction_ws, 0.0f)).xyz);
        if (dot(view_space_light_direction, view_space_light_direction) > 0.5f
            && IsAabbOutsideInfiniteAcuteConeApprox(
                view_space_light_position,
                view_space_light_direction,
                tan_cone_angle,
                view_tile_center,
                view_tile_extent)) {
            return false;
        }
    }

    return true;
}

[shader("compute")]
[numthreads(
    LIGHT_GRID_THREADGROUP_SIZE,
    LIGHT_GRID_THREADGROUP_SIZE,
    LIGHT_GRID_THREADGROUP_SIZE)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<LightCullingPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    const uint3 cluster_dims = uint3(
        pass_constants.cluster_dim_x,
        pass_constants.cluster_dim_y,
        pass_constants.cluster_dim_z);
    if (any(dispatch_thread_id >= cluster_dims)
        || pass_constants.num_lights == 0u
        || !BX_IsValidSlot(pass_constants.light_buffer_index)
        || !BX_IsValidSlot(pass_constants.light_list_uav_index)
        || !BX_IsValidSlot(pass_constants.light_count_uav_index)
        || pass_constants.max_lights_per_cell == 0u
        || pass_constants.light_grid_pixel_size_shift == 0u) {
        return;
    }

    StructuredBuffer<PositionalLightData> lights
        = ResourceDescriptorHeap[pass_constants.light_buffer_index];
    RWStructuredBuffer<uint2> cluster_grid
        = ResourceDescriptorHeap[pass_constants.light_count_uav_index];
    RWStructuredBuffer<uint> light_list
        = ResourceDescriptorHeap[pass_constants.light_list_uav_index];

    const float3 light_grid_z_params = float3(
        pass_constants.light_grid_z_params_b,
        pass_constants.light_grid_z_params_o,
        pass_constants.light_grid_z_params_s);
    const uint cluster_index = ComputeLinearClusterIndex(
        dispatch_thread_id, cluster_dims);
    const uint light_list_offset
        = cluster_index * pass_constants.max_lights_per_cell;

    float3 view_tile_min = 0.0.xxx;
    float3 view_tile_max = 0.0.xxx;
    ComputeCellViewAabb(
        dispatch_thread_id,
        pass_constants.screen_dimensions,
        pass_constants.inv_projection_matrix,
        pass_constants.light_grid_pixel_size_shift,
        light_grid_z_params,
        pass_constants.cluster_dim_z,
        view_tile_min,
        view_tile_max);

    const float3 view_tile_center = 0.5f * (view_tile_min + view_tile_max);
    const float3 view_tile_extent = view_tile_max - view_tile_center;

    uint light_count = 0u;
    [loop]
    for (uint light_index = 0u; light_index < pass_constants.num_lights; ++light_index) {
        const PositionalLightData light = lights[light_index];
        if (!TestLightAgainstCell(light, view_tile_center, view_tile_extent)) {
            continue;
        }

        if (light_count < pass_constants.max_lights_per_cell) {
            light_list[light_list_offset + light_count] = light_index;
        }
        ++light_count;
    }

    cluster_grid[cluster_index] = uint2(
        light_list_offset,
        min(light_count, pass_constants.max_lights_per_cell));
}

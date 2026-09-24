//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_LIGHT_GRID_DATA_HLSLI
#define OXYGEN_VORTEX_LIGHT_GRID_DATA_HLSLI

#include "Vortex/Contracts/Lighting/LightingIndices.hlsli"

static const uint LIGHT_GRID_PERSPECTIVE = 0u;
static const uint LIGHT_GRID_ORTHOGRAPHIC = 1u;

// Matches Vortex/Lighting/Types/LightGridMetadata.h (64 bytes).
struct LightGridMetadata {
    uint3 grid_size;
    uint pixel_size_shift;
    float2 content_origin_px;
    float2 content_extent_px;
    float3 grid_z_params;
    float far_depth_m;
    float near_depth_m;
    uint projection_kind;
    uint2 reserved;
};

// Matches Vortex/Lighting/Types/ClusterLightRange.h (8 bytes).
struct ClusterLightRange {
    uint offset;
    uint count;
};

static const uint LIGHT_GRID_BUILD_PENDING = 0u;
static const uint LIGHT_GRID_BUILD_VALID = 1u;
static const uint LIGHT_GRID_BUILD_FAILED = 2u;
static const uint LIGHT_GRID_REASON_NONE = 0u;
static const uint LIGHT_GRID_REASON_INVALID_BOUNDS = 1u;
static const uint LIGHT_GRID_REASON_INVALID_INDEX = 2u;
static const uint LIGHT_GRID_REASON_CAPACITY = 3u;
static const uint LIGHT_GRID_REASON_GENERATION_MISMATCH = 4u;

// Matches Vortex/Lighting/Types/LightGridBuildStatus.h (32 bytes).
struct LightGridBuildStatus {
    uint state;
    uint reason;
    uint written_index_count;
    uint fallback_cell_count;
    uint2 required_index_count;
    uint2 selection_revision;
};

// Matches Vortex/Lighting/Types/LightGridPassConstants.h (208 bytes).
struct LightGridPassConstants {
    uint lighting_bindings_srv;
    uint ranges_uav;
    uint indices_uav;
    uint status_uav;
    uint counts_uav;
    uint offsets_uav;
    uint subpass;
    uint work_count;
    uint2 work_offset;
    uint scan_stride;
    uint scan_phase;
    uint scan_destination_uav;
    uint3 reserved;
    float4x4 view_matrix;
    float4x4 inverse_projection;
    float4 depth_projection;
};

#endif

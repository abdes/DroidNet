//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_PROJECTEDLOCALSHADOWRECORD_HLSLI
#define OXYGEN_VORTEX_PROJECTEDLOCALSHADOWRECORD_HLSLI

// Matches Vortex/Shadows/Types/ProjectedLocalShadowRecord.h (128 bytes).
struct ProjectedLocalShadowRecord {
    float4x4 light_view_projection;
    float3 shadow_origin_ws;
    float near_plane_m;
    float far_plane_m;
    float normal_bias_m;
    float depth_bias;
    float world_texel_size;
    uint surface_srv;
    uint array_layer;
    uint selection_index;
    uint reserved0;
    float2 inverse_resolution;
    uint2 reserved1;
};

#endif

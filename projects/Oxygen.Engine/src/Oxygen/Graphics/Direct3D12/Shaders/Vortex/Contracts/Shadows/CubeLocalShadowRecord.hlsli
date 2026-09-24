//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_CUBELOCALSHADOWRECORD_HLSLI
#define OXYGEN_VORTEX_CUBELOCALSHADOWRECORD_HLSLI

// Matches Vortex/Shadows/Types/CubeLocalShadowRecord.h (448 bytes).
struct CubeLocalShadowRecord {
    float4x4 face_light_view_projection[6];
    float3 shadow_origin_ws;
    float near_plane_m;
    float far_plane_m;
    float normal_bias_m;
    float depth_bias;
    float world_texel_size;
    uint surface_srv;
    uint first_array_layer;
    uint selection_index;
    float shadow_strength;
    float2 inverse_resolution;
    uint2 reserved1;
};

#endif

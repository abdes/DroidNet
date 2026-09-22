//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SHADOW_CASCADE_BINDING_HLSLI
#define OXYGEN_VORTEX_SHADOW_CASCADE_BINDING_HLSLI

// Matches Vortex/Shadows/Types/ShadowCascadeBinding.h (128 bytes).
struct VortexShadowCascadeBinding {
    float4x4 light_view_projection;
    float split_near;
    float split_far;
    float depth_bias;
    float normal_bias_m;
    uint surface_srv;
    uint array_layer;
    uint2 reserved0;
    float2 inverse_resolution;
    float world_texel_size;
    float transition_width;
    float fade_begin;
    float fade_end;
    uint2 reserved1;
};

#endif

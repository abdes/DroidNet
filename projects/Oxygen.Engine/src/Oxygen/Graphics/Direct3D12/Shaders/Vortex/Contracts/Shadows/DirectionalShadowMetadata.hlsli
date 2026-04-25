//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALSHADOWMETADATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALSHADOWMETADATA_HLSLI

#ifndef OXYGEN_MAX_SHADOW_CASCADES
#define OXYGEN_MAX_SHADOW_CASCADES 4
#endif

struct DirectionalShadowMetadata
{
    uint shadow_instance_index;
    uint implementation_kind;
    float constant_bias;
    float normal_bias;

    uint cascade_count;
    uint flags;
    uint split_mode;
    uint resource_index;

    float distribution_exponent;
    float max_shadow_distance;
    float distance_fadeout_begin;
    float _padding0;

    float cascade_distances[OXYGEN_MAX_SHADOW_CASCADES];
    float cascade_transition_widths[OXYGEN_MAX_SHADOW_CASCADES];
    float cascade_world_texel_size[OXYGEN_MAX_SHADOW_CASCADES];
    float4x4 cascade_view_proj[OXYGEN_MAX_SHADOW_CASCADES];
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALSHADOWMETADATA_HLSLI

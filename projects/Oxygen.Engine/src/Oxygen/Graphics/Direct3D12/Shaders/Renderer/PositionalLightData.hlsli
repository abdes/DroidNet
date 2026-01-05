//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_POSITIONALLIGHTDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_POSITIONALLIGHTDATA_HLSLI

static const uint POSITIONAL_LIGHT_TYPE_SHIFT = 0u;
static const uint POSITIONAL_LIGHT_TYPE_MASK = 0x3u << POSITIONAL_LIGHT_TYPE_SHIFT;
static const uint POSITIONAL_LIGHT_TYPE_POINT = 0u << POSITIONAL_LIGHT_TYPE_SHIFT;
static const uint POSITIONAL_LIGHT_TYPE_SPOT = 1u << POSITIONAL_LIGHT_TYPE_SHIFT;

static const uint POSITIONAL_LIGHT_FLAG_AFFECTS_WORLD = 1u << 2;
static const uint POSITIONAL_LIGHT_FLAG_CASTS_SHADOWS = 1u << 3;
static const uint POSITIONAL_LIGHT_FLAG_CONTACT_SHADOWS = 1u << 4;

struct PositionalLightData
{
    float3 position_ws;
    float range;

    float3 color_rgb;
    float intensity;

    float3 direction_ws;
    uint flags; // See POSITIONAL_LIGHT_FLAG_*

    float inner_cone_cos;
    float outer_cone_cos;
    float source_radius;
    float decay_exponent;

    uint attenuation_model;
    uint mobility;
    uint shadow_resolution_hint;
    uint shadow_flags;

    float shadow_bias;
    float shadow_normal_bias;
    float exposure_compensation_ev;
    float _pad0;

    uint shadow_map_index;
    uint _pad1;
    uint _pad2;
    uint _pad3;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_POSITIONALLIGHTDATA_HLSLI

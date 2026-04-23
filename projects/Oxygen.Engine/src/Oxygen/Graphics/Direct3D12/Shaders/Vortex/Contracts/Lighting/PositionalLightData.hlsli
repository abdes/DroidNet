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
    // Register 0
    float3 position_ws;
    float range;

    // Register 1
    float3 color_rgb;
    float luminous_flux_lm;  // Luminous flux in lumens (lm)

    // Register 2
    float3 direction_ws;
    uint flags; // See POSITIONAL_LIGHT_FLAG_*

    // Register 3
    float inner_cone_cos;
    float outer_cone_cos;
    float source_radius;
    float decay_exponent;

    // Register 4
    uint attenuation_model;
    uint mobility;
    uint shadow_resolution_hint;
    uint shadow_flags;

    // Register 5
    float shadow_bias;
    float shadow_normal_bias;
    float exposure_compensation_ev;
    uint shadow_map_index;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_POSITIONALLIGHTDATA_HLSLI

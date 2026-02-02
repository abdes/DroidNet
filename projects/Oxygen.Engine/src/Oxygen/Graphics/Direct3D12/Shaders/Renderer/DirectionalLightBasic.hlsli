//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALLIGHTBASIC_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALLIGHTBASIC_HLSLI

static const uint DIRECTIONAL_LIGHT_FLAG_AFFECTS_WORLD = 1u << 0;
static const uint DIRECTIONAL_LIGHT_FLAG_CASTS_SHADOWS = 1u << 1;
static const uint DIRECTIONAL_LIGHT_FLAG_CONTACT_SHADOWS = 1u << 2;
static const uint DIRECTIONAL_LIGHT_FLAG_ENV_CONTRIBUTION = 1u << 3;
static const uint DIRECTIONAL_LIGHT_FLAG_SUN_LIGHT = 1u << 4;

struct DirectionalLightBasic
{
    float3 color_rgb;
    float intensity_lux;  // Illuminance in lux (lm/m²)
    float3 direction_ws;
    float angular_size_radians;

    uint shadow_index;
    uint flags; // See DIRECTIONAL_LIGHT_FLAG_*
    uint _reserved0;
    uint _reserved1;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_DIRECTIONALLIGHTBASIC_HLSLI

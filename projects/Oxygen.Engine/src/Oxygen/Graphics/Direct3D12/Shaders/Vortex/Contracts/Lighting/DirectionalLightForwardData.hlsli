//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_DIRECTIONALLIGHTFORWARDDATA_HLSLI
#define OXYGEN_VORTEX_DIRECTIONALLIGHTFORWARDDATA_HLSLI

// Canonical 64-byte structured-buffer record; no physical-unit conversion here.
struct DirectionalLightForwardData
{
    float3 direction_to_source_ws;
    uint atmosphere_light_slot;
    float3 illuminance_rgb_lux;
    uint flags;
    float3 ground_transmittance_rgb;
    uint atmosphere_mode_flags;
    uint selection_index;
    uint3 reserved;
};

#endif

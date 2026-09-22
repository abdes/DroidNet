//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_FORWARDLOCALLIGHTRECORD_HLSLI
#define OXYGEN_VORTEX_FORWARDLOCALLIGHTRECORD_HLSLI

// Canonical 80-byte structured-buffer record; no physical-unit conversion here.
struct ForwardLocalLightRecord
{
    float3 position_ws;
    float range_m;
    float3 intensity_rgb_cd;
    float source_radius_m;
    float3 emitted_direction_ws;
    float inverse_range_m;
    float inner_cone_sin_half_squared;
    float outer_cone_sin_half_squared;
    uint kind;
    uint flags;
    uint selection_index;
    uint3 reserved;
};

static const uint FORWARD_LOCAL_LIGHT_POINT = 0u;
static const uint FORWARD_LOCAL_LIGHT_SPOT = 1u;
static const uint FORWARD_LOCAL_LIGHT_CASTS_SHADOWS = 1u;

#endif

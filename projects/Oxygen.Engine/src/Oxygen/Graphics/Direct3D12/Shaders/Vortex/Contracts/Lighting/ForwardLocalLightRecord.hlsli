//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_FORWARD_LOCAL_LIGHT_RECORD_HLSLI
#define OXYGEN_VORTEX_FORWARD_LOCAL_LIGHT_RECORD_HLSLI

// Matches Vortex/Lighting/Types/ForwardLocalLightRecord.h: 96 bytes.
// Selection has already excluded lights that do not affect the world.
struct ForwardLocalLightRecord
{
    float4 position_and_inv_radius;
    float4 color_id_falloff_and_ray_bias;
    float4 direction_and_extra_data;
    float4 spot_angles_and_source_radius;
    float4 tangent_ies_and_specular_scale;
    float4 rect_data_and_linkage; // numeric kind, canonical flags, range, reserved
};

static const uint FORWARD_LOCAL_LIGHT_POINT = 0u;
static const uint FORWARD_LOCAL_LIGHT_SPOT = 1u;
static const uint FORWARD_LOCAL_LIGHT_CASTS_SHADOWS = 1u;

#endif

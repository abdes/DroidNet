//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

static const uint ENVIRONMENT_VIEW_FLAG_HEIGHT_FOG = 1u << 2u;

struct EnvironmentViewData
{
    uint flags;
    uint transform_mode;
    uint atmosphere_light_count;
    uint _pad0;
    float sky_view_lut_slice;
    float planet_to_sun_cos_zenith;
    float aerial_perspective_distance_scale;
    float aerial_scattering_strength;
    float4 planet_center_ws_pad;
    float4 planet_up_ws_camera_altitude_km;
    float4 sky_planet_translated_world_center_km_and_view_height_km;
    float4 sky_camera_translated_world_origin_km_pad;
    float4 sky_view_lut_referential_row0;
    float4 sky_view_lut_referential_row1;
    float4 sky_view_lut_referential_row2;
    float4 atmosphere_light0_direction_angular_size;
    float4 atmosphere_light0_disk_luminance_rgb;
    float4 sky_luminance_factor_height_fog_contribution;
    float4 atmosphere_light1_direction_angular_size;
    float4 atmosphere_light1_disk_luminance_rgb;
    float4 sky_aerial_luminance_aerial_start_depth_km;
    float4 trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass;
    float4 camera_aerial_volume_depth_params;
    float4 height_fog_light0_illuminance_enabled;
    float4 height_fog_light1_illuminance_enabled;
};


// Load the environment view data at `slot`; false, with zeroed data (no
// environment products), when the slot is missing or outside the global SRV
// domain. There is no default planet: consumers gate on the result or flags.
static bool TryLoadEnvironmentViewData(uint slot, out EnvironmentViewData data)
{
    data = (EnvironmentViewData)0;
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return false;
    }
    StructuredBuffer<EnvironmentViewData> data_buffer = ResourceDescriptorHeap[slot];
    data = data_buffer[0];
    return true;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI

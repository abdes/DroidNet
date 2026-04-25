//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

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
};


static EnvironmentViewData LoadEnvironmentViewData(uint slot)
{
    EnvironmentViewData invalid_data = (EnvironmentViewData)0;
    invalid_data.flags = 0u;
    invalid_data.sky_view_lut_slice = 0.0f;
    invalid_data.planet_to_sun_cos_zenith = 0.0f;
    invalid_data.aerial_perspective_distance_scale = 1.0f;
    invalid_data.aerial_scattering_strength = 1.0f;
    // Keep the invalid-view fallback aligned with the C++ default
    // EnvironmentViewData contract: world origin sits on the planet surface and
    // the planet center is below it on -Z.
    invalid_data.planet_center_ws_pad = float4(0.0f, 0.0f, -6360000.0f, 0.0f);
    invalid_data.planet_up_ws_camera_altitude_km = float4(0.0f, 0.0f, 1.0f, 0.0f);
    invalid_data.sky_planet_translated_world_center_km_and_view_height_km
        = float4(0.0f, 0.0f, -6360.0f, 6360.0f);
    invalid_data.sky_camera_translated_world_origin_km_pad = float4(0.0f, 0.0f, 0.0f, 0.0f);
    invalid_data.sky_view_lut_referential_row0 = float4(1.0f, 0.0f, 0.0f, 0.0f);
    invalid_data.sky_view_lut_referential_row1 = float4(0.0f, 1.0f, 0.0f, 0.0f);
    invalid_data.sky_view_lut_referential_row2 = float4(0.0f, 0.0f, 1.0f, 0.0f);
    invalid_data.atmosphere_light0_direction_angular_size = float4(0.0f, 0.0f, 1.0f, 0.0f);
    invalid_data.atmosphere_light0_disk_luminance_rgb = float4(0.0f, 0.0f, 0.0f, 0.0f);
    invalid_data.sky_luminance_factor_height_fog_contribution = float4(1.0f, 1.0f, 1.0f, 1.0f);
    invalid_data.atmosphere_light1_direction_angular_size = float4(0.0f, 0.0f, 1.0f, 0.0f);
    invalid_data.atmosphere_light1_disk_luminance_rgb = float4(0.0f, 0.0f, 0.0f, 0.0f);
    invalid_data.sky_aerial_luminance_aerial_start_depth_km = float4(1.0f, 1.0f, 1.0f, 0.1f);
    invalid_data.trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass = float4(1.0f, -90.0f, 0.0f, 1.0f);
    invalid_data.camera_aerial_volume_depth_params = float4(32.0f, 1.0f / 32.0f, 3.0f, 1.0f / 3.0f);

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_data;
    }

    StructuredBuffer<EnvironmentViewData> data_buffer = ResourceDescriptorHeap[slot];
    return data_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWDATA_HLSLI

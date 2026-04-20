//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWHELPERS_HLSLI

#include "Renderer/EnvironmentFrameBindings.hlsli"
#include "Renderer/ViewConstants.hlsli"
#include "Renderer/EnvironmentViewData.hlsli"
#include "Renderer/ViewFrameBindings.hlsli"

static inline EnvironmentViewData LoadResolvedEnvironmentViewData()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    if (view_bindings.environment_frame_slot == K_INVALID_BINDLESS_INDEX) {
        return LoadEnvironmentViewData(K_INVALID_BINDLESS_INDEX);
    }

    const EnvironmentFrameBindings environment_bindings =
        LoadEnvironmentFrameBindings(view_bindings.environment_frame_slot);
    return LoadEnvironmentViewData(environment_bindings.environment_view_slot);
}

static inline float3 GetPlanetCenterWS()
{
    return LoadResolvedEnvironmentViewData().planet_center_ws_pad.xyz;
}

static inline float3 GetPlanetUpWS()
{
    return LoadResolvedEnvironmentViewData().planet_up_ws_camera_altitude_m.xyz;
}

static inline float GetCameraAltitudeM()
{
    return LoadResolvedEnvironmentViewData().planet_up_ws_camera_altitude_m.w;
}

static inline float GetAerialPerspectiveDistanceScale()
{
    return LoadResolvedEnvironmentViewData().aerial_perspective_distance_scale;
}

static inline float GetAerialScatteringStrength()
{
    return LoadResolvedEnvironmentViewData().aerial_scattering_strength;
}

static inline uint GetAtmosphereFlags()
{
    return LoadResolvedEnvironmentViewData().flags;
}

static inline float GetSkyViewLutSlice()
{
    return LoadResolvedEnvironmentViewData().sky_view_lut_slice;
}

static inline float GetPlanetToSunCosZenith()
{
    return LoadResolvedEnvironmentViewData().planet_to_sun_cos_zenith;
}

static inline float3 GetSkyPlanetTranslatedWorldCenter()
{
    return LoadResolvedEnvironmentViewData()
        .sky_planet_translated_world_center_and_view_height.xyz;
}

static inline float GetSkyViewHeight()
{
    return LoadResolvedEnvironmentViewData()
        .sky_planet_translated_world_center_and_view_height.w;
}

static inline float3 GetSkyCameraTranslatedWorldOrigin()
{
    return LoadResolvedEnvironmentViewData().sky_camera_translated_world_origin_pad.xyz;
}

static inline float3x3 GetSkyViewLutReferentialRows()
{
    const EnvironmentViewData data = LoadResolvedEnvironmentViewData();
    return float3x3(
        data.sky_view_lut_referential_row0.xyz,
        data.sky_view_lut_referential_row1.xyz,
        data.sky_view_lut_referential_row2.xyz);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTVIEWHELPERS_HLSLI

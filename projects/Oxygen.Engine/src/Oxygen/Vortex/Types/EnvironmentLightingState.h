//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex {

//! Published environment rendering diagnostics, independent of scene-renderer
//! implementation.
struct EnvironmentLightingState {
  bool published_bindings { false };
  bool owned_by_environment_service { false };
  bool stage14_requested { false };
  bool stage14_local_fog_requested { false };
  bool stage14_local_fog_executed { false };
  bool stage14_local_fog_hzb_consumed { false };
  bool stage14_local_fog_hzb_unavailable { false };
  bool stage14_local_fog_buffer_ready { false };
  bool stage14_local_fog_skipped { false };
  std::uint32_t stage14_local_fog_instance_count { 0U };
  std::uint32_t stage14_local_fog_dispatch_count_x { 0U };
  std::uint32_t stage14_local_fog_dispatch_count_y { 0U };
  std::uint32_t stage14_local_fog_dispatch_count_z { 0U };
  bool stage14_volumetric_fog_requested { false };
  bool stage14_volumetric_fog_executed { false };
  bool stage14_integrated_light_scattering_valid { false };
  ShaderVisibleIndex stage14_integrated_light_scattering_srv {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t stage14_volumetric_fog_grid_width { 0U };
  std::uint32_t stage14_volumetric_fog_grid_height { 0U };
  std::uint32_t stage14_volumetric_fog_grid_depth { 0U };
  std::uint32_t stage14_volumetric_fog_dispatch_count_x { 0U };
  std::uint32_t stage14_volumetric_fog_dispatch_count_y { 0U };
  std::uint32_t stage14_volumetric_fog_dispatch_count_z { 0U };
  bool stage14_volumetric_fog_height_fog_media_requested { false };
  bool stage14_volumetric_fog_height_fog_media_executed { false };
  bool stage14_volumetric_fog_sky_light_injection_requested { false };
  bool stage14_volumetric_fog_sky_light_injection_executed { false };
  bool stage14_volumetric_fog_temporal_history_requested { false };
  bool stage14_volumetric_fog_temporal_history_reprojection_executed {
    false,
  };
  bool stage14_volumetric_fog_temporal_history_reset { false };
  bool stage14_volumetric_fog_local_fog_injection_requested { false };
  bool stage14_volumetric_fog_local_fog_injection_executed { false };
  std::uint32_t stage14_volumetric_fog_local_fog_instance_count { 0U };
  bool stage15_requested { false };
  bool sky_requested { false };
  bool sky_executed { false };
  std::uint32_t sky_draw_count { 0U };
  bool atmosphere_requested { false };
  bool atmosphere_executed { false };
  std::uint32_t atmosphere_draw_count { 0U };
  bool fog_requested { false };
  bool fog_executed { false };
  std::uint32_t fog_draw_count { 0U };
  std::uint32_t total_draw_count { 0U };
  bool ambient_bridge_published { false };
  std::uint32_t probe_revision { 0U };
  ShaderVisibleIndex published_environment_frame_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex ambient_bridge_irradiance_srv {
    kInvalidShaderVisibleIndex
  };
};

} // namespace oxygen::vortex

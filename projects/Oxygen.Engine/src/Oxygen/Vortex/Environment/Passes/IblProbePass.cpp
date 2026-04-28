//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>

namespace oxygen::vortex::environment {

namespace {

  auto ProbeBindingsHaveUsableResources(const EnvironmentProbeBindings& probes)
    -> bool
  {
    return probes.environment_map_srv.IsValid()
      && probes.diffuse_sh_srv.IsValid() && probes.probe_revision != 0U;
  }

  auto ResetProbeResourceSlots(EnvironmentProbeBindings& probes) -> void
  {
    probes.environment_map_srv = kInvalidShaderVisibleIndex;
    probes.diffuse_sh_srv = kInvalidShaderVisibleIndex;
    probes.irradiance_map_srv = kInvalidShaderVisibleIndex;
    probes.prefiltered_map_srv = kInvalidShaderVisibleIndex;
    probes.brdf_lut_srv = kInvalidShaderVisibleIndex;
  }

  auto MakeProductKey(const SkyLightEnvironmentModel& sky_light)
    -> StaticSkyLightProductKey
  {
    return {
      .source_cubemap = sky_light.cubemap_resource,
      .source_revision = 0U,
      .output_face_size = 0U,
      .source_format_class = 0U,
      .source_rotation_radians = sky_light.source_cubemap_angle_radians,
      .lower_hemisphere_solid_color = sky_light.lower_hemisphere_is_solid_color,
      .lower_hemisphere_color = sky_light.lower_hemisphere_color,
      .lower_hemisphere_blend_alpha = sky_light.lower_hemisphere_blend_alpha,
    };
  }

  auto DisableStaticSkyLightProducts(EnvironmentProbeState& state) -> void
  {
    ResetProbeResourceSlots(state.probes);
    state.static_sky_light = StaticSkyLightProducts {};
    state.static_sky_light.status = StaticSkyLightProductStatus::kDisabled;
    state.static_sky_light.unavailable_reason
      = StaticSkyLightUnavailableReason::kNone;
    state.valid = false;
    state.flags = 0U;
  }

  auto MarkStaticSkyLightUnavailable(EnvironmentProbeState& state,
    const StaticSkyLightUnavailableReason reason,
    const StaticSkyLightProductKey& key = {}) -> void
  {
    ResetProbeResourceSlots(state.probes);
    state.static_sky_light = StaticSkyLightProducts {};
    state.static_sky_light.key = key;
    state.static_sky_light.status = StaticSkyLightProductStatus::kUnavailable;
    state.static_sky_light.unavailable_reason = reason;
    state.valid = false;
    state.flags = kEnvironmentProbeStateFlagUnavailable;
  }

} // namespace

auto IblProbePass::Refresh(const EnvironmentProbeState& current_state,
  const bool environment_source_changed) const -> RefreshState
{
  auto next_state = current_state;
  if (environment_source_changed) {
    next_state.probes.probe_revision += 1U;
    ResetProbeResourceSlots(next_state.probes);
    next_state.static_sky_light.status
      = StaticSkyLightProductStatus::kUnavailable;
    next_state.static_sky_light.unavailable_reason
      = StaticSkyLightUnavailableReason::kProcessingFailed;
    next_state.valid = false;
    next_state.flags
      = kEnvironmentProbeStateFlagUnavailable | kEnvironmentProbeStateFlagStale;
  } else if (next_state.valid
    && ProbeBindingsHaveUsableResources(next_state.probes)) {
    next_state.flags = kEnvironmentProbeStateFlagResourcesValid;
  } else {
    next_state.valid = false;
    next_state.flags = kEnvironmentProbeStateFlagUnavailable;
  }

  return {
    .requested = environment_source_changed,
    .refreshed = environment_source_changed,
    .probe_state = next_state,
  };
}

auto IblProbePass::RefreshStaticSkyLight(
  const EnvironmentProbeState& current_state,
  const SkyLightEnvironmentModel& sky_light) const -> RefreshState
{
  auto next_state = current_state;
  const auto previous_revision = next_state.probes.probe_revision;

  if (!sky_light.enabled || !sky_light.affect_global_illumination
    || sky_light.diffuse_intensity <= 0.0F) {
    DisableStaticSkyLightProducts(next_state);
  } else if (sky_light.source == kSkyLightSourceCapturedScene) {
    MarkStaticSkyLightUnavailable(
      next_state, StaticSkyLightUnavailableReason::kCapturedSceneDeferred);
  } else if (sky_light.real_time_capture_enabled) {
    MarkStaticSkyLightUnavailable(next_state,
      StaticSkyLightUnavailableReason::kRealTimeCaptureDeferred,
      MakeProductKey(sky_light));
  } else if (sky_light.source == kSkyLightSourceSpecifiedCubemap
    && sky_light.cubemap_resource.get() == 0U) {
    MarkStaticSkyLightUnavailable(next_state,
      StaticSkyLightUnavailableReason::kMissingCubemap,
      MakeProductKey(sky_light));
  } else if (sky_light.source == kSkyLightSourceSpecifiedCubemap) {
    const auto key = MakeProductKey(sky_light);
    MarkStaticSkyLightUnavailable(next_state,
      StaticSkyLightUnavailableReason::kShaderConsumerMigrationIncomplete, key);
  } else {
    MarkStaticSkyLightUnavailable(
      next_state, StaticSkyLightUnavailableReason::kUnsupportedFormat);
  }

  if (next_state.static_sky_light.status
      != current_state.static_sky_light.status
    || next_state.static_sky_light.unavailable_reason
      != current_state.static_sky_light.unavailable_reason
    || next_state.static_sky_light.key != current_state.static_sky_light.key) {
    next_state.probes.probe_revision += 1U;
    if (next_state.static_sky_light.status
      == StaticSkyLightProductStatus::kUnavailable) {
      next_state.flags |= kEnvironmentProbeStateFlagStale;
    }
  }

  if (next_state.valid && ProbeBindingsHaveUsableResources(next_state.probes)) {
    next_state.flags = kEnvironmentProbeStateFlagResourcesValid;
    next_state.static_sky_light.status
      = StaticSkyLightProductStatus::kValidCurrentKey;
    next_state.static_sky_light.unavailable_reason
      = StaticSkyLightUnavailableReason::kNone;
  }

  return {
    .requested = next_state.probes.probe_revision != previous_revision,
    .refreshed = next_state.probes.probe_revision != previous_revision,
    .probe_state = next_state,
  };
}

} // namespace oxygen::vortex::environment

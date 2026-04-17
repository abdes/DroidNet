//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>

#include <memory>

#include <Oxygen/Vortex/Environment/Internal/AtmosphereRenderer.h>
#include <Oxygen/Vortex/Environment/Internal/FogRenderer.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/SkyRenderer.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex {

EnvironmentLightingService::EnvironmentLightingService(Renderer& renderer)
  : renderer_(renderer)
  , sky_(std::make_unique<environment::SkyRenderer>(renderer))
  , atmosphere_(std::make_unique<environment::AtmosphereRenderer>(renderer))
  , fog_(std::make_unique<environment::FogRenderer>(renderer))
  , ibl_(std::make_unique<environment::internal::IblProcessor>(renderer))
{
}

EnvironmentLightingService::~EnvironmentLightingService() = default;

auto EnvironmentLightingService::EnsurePublishResources() -> bool
{
  if (bindings_publisher_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  bindings_publisher_ = std::make_unique<
    internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>(
    observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
    observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
    "EnvironmentFrameBindings");
  return true;
}

auto EnvironmentLightingService::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  published_views_.clear();
  last_probe_refresh_state_ = {
    .frame_sequence = sequence,
    .frame_slot = slot,
    .valid = probe_state_.valid,
    .probe_revision = probe_state_.probes.probe_revision,
  };
  last_publication_state_ = {
    .frame_sequence = sequence,
    .frame_slot = slot,
    .probe_revision = probe_state_.probes.probe_revision,
  };
  last_stage15_state_ = {};
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
  }
}

auto EnvironmentLightingService::RefreshPersistentProbeState(
  const bool environment_source_changed) -> void
{
  const auto refreshed
    = ibl_->RefreshPersistentProbes(probe_state_, environment_source_changed);
  probe_state_ = refreshed.probe_state;
  last_probe_refresh_state_ = {
    .frame_sequence = current_sequence_,
    .frame_slot = current_slot_,
    .requested = refreshed.requested,
    .refreshed = refreshed.refreshed,
    .valid = refreshed.probe_state.valid,
    .probe_revision = refreshed.probe_state.probes.probe_revision,
  };
  last_publication_state_.probe_revision = probe_state_.probes.probe_revision;
}

auto EnvironmentLightingService::BuildBindings(
  const ShaderVisibleIndex environment_static_slot,
  const ShaderVisibleIndex environment_view_slot,
  const bool enable_ambient_bridge) const -> EnvironmentFrameBindings
{
  auto bindings = EnvironmentFrameBindings {
    .environment_static_slot = environment_static_slot,
    .environment_view_slot = environment_view_slot,
    .probes = probe_state_.probes,
    .evaluation = EnvironmentEvaluationParameters {},
    .ambient_bridge = EnvironmentAmbientBridgeBindings {},
  };

  if (enable_ambient_bridge) {
    bindings.evaluation.flags |= kEnvironmentEvaluationFlagAmbientBridgeEligible;
  }
  if (enable_ambient_bridge && probe_state_.valid) {
    bindings.ambient_bridge.irradiance_map_srv = probe_state_.probes.irradiance_map_srv;
    bindings.ambient_bridge.ambient_intensity = bindings.evaluation.ambient_intensity;
    bindings.ambient_bridge.average_brightness
      = bindings.evaluation.average_brightness;
    bindings.ambient_bridge.blend_fraction = bindings.evaluation.blend_fraction;
    bindings.ambient_bridge.flags = kEnvironmentAmbientBridgeFlagEnabled;
  }

  return bindings;
}

auto EnvironmentLightingService::PublishEnvironmentBindings(RenderContext& ctx,
  const ShaderVisibleIndex environment_static_slot,
  const ShaderVisibleIndex environment_view_slot,
  const bool enable_ambient_bridge) -> ShaderVisibleIndex
{
  if (ctx.current_view.view_id == kInvalidViewId || !EnsurePublishResources()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto bindings = BuildBindings(
    environment_static_slot, environment_view_slot, enable_ambient_bridge);
  const auto slot
    = bindings_publisher_->Publish(ctx.current_view.view_id, bindings);
  published_views_.insert_or_assign(
    ctx.current_view.view_id, PublishedView { .slot = slot, .bindings = bindings });

  last_publication_state_.frame_sequence = current_sequence_;
  last_publication_state_.frame_slot = current_slot_;
  last_publication_state_.published_view_count += 1U;
  last_publication_state_.probe_revision = bindings.probes.probe_revision;
  if (bindings.ambient_bridge.flags != 0U) {
    last_publication_state_.ambient_bridge_view_count += 1U;
  }

  return slot;
}

auto EnvironmentLightingService::RenderSkyAndFog(
  RenderContext& ctx, const SceneTextures& scene_textures) -> void
{
  const auto sky_state = sky_->Render(ctx, scene_textures);
  const auto atmosphere_state = atmosphere_->Render(ctx, scene_textures);
  const auto fog_state = fog_->Render(ctx, scene_textures);
  last_stage15_state_ = {
    .view_id = ctx.current_view.view_id,
    .requested = sky_state.requested || atmosphere_state.requested
      || fog_state.requested,
    .sky_requested = sky_state.requested,
    .sky_executed = sky_state.executed,
    .atmosphere_requested = atmosphere_state.requested,
    .atmosphere_executed = atmosphere_state.executed,
    .fog_requested = fog_state.requested,
    .fog_executed = fog_state.executed,
  };
}

auto EnvironmentLightingService::InspectBindings(const ViewId view_id) const
  -> const EnvironmentFrameBindings*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.bindings : nullptr;
}

auto EnvironmentLightingService::ResolveEnvironmentFrameSlot(const ViewId view_id) const
  -> ShaderVisibleIndex
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.slot
                                      : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
}

} // namespace oxygen::vortex

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace internal {
template <typename Payload> class PerViewStructuredPublisher;
}

namespace environment {
class SkyRenderer;
class AtmosphereRenderer;
class FogRenderer;
namespace internal {
class IblProcessor;
}
} // namespace environment

class EnvironmentLightingService {
public:
  struct ProbeRefreshState {
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    bool requested { false };
    bool refreshed { false };
    bool valid { false };
    std::uint32_t probe_revision { 0U };
  };

  struct PublicationState {
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    std::uint32_t published_view_count { 0U };
    std::uint32_t ambient_bridge_view_count { 0U };
    std::uint32_t probe_revision { 0U };
  };

  struct Stage15State {
    ViewId view_id { kInvalidViewId };
    bool requested { false };
    bool sky_requested { false };
    bool sky_executed { false };
    bool atmosphere_requested { false };
    bool atmosphere_executed { false };
    bool fog_requested { false };
    bool fog_executed { false };
  };

  OXGN_VRTX_API explicit EnvironmentLightingService(Renderer& renderer);
  OXGN_VRTX_API ~EnvironmentLightingService();

  EnvironmentLightingService(const EnvironmentLightingService&) = delete;
  auto operator=(const EnvironmentLightingService&) -> EnvironmentLightingService& = delete;
  EnvironmentLightingService(EnvironmentLightingService&&) = delete;
  auto operator=(EnvironmentLightingService&&) -> EnvironmentLightingService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto RefreshPersistentProbeState(bool environment_source_changed) -> void;
  [[nodiscard]] OXGN_VRTX_API auto BuildBindings(
    ShaderVisibleIndex environment_static_slot,
    ShaderVisibleIndex environment_view_slot,
    bool enable_ambient_bridge) const -> EnvironmentFrameBindings;
  OXGN_VRTX_API auto PublishEnvironmentBindings(RenderContext& ctx,
    ShaderVisibleIndex environment_static_slot = kInvalidShaderVisibleIndex,
    ShaderVisibleIndex environment_view_slot = kInvalidShaderVisibleIndex,
    bool enable_ambient_bridge = false) -> ShaderVisibleIndex;
  OXGN_VRTX_API auto RenderSkyAndFog(
    RenderContext& ctx, const SceneTextures& scene_textures) -> void;

  [[nodiscard]] OXGN_VRTX_API auto InspectBindings(ViewId view_id) const
    -> const EnvironmentFrameBindings*;
  [[nodiscard]] OXGN_VRTX_API auto ResolveEnvironmentFrameSlot(ViewId view_id) const
    -> ShaderVisibleIndex;
  [[nodiscard]] OXGN_VRTX_NDAPI auto InspectProbeState() const noexcept
    -> const EnvironmentProbeState&
  {
    return probe_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastProbeRefreshState() const noexcept
    -> const ProbeRefreshState&
  {
    return last_probe_refresh_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastPublicationState() const noexcept
    -> const PublicationState&
  {
    return last_publication_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastStage15State() const noexcept
    -> const Stage15State&
  {
    return last_stage15_state_;
  }

private:
  struct PublishedView {
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
    EnvironmentFrameBindings bindings {};
  };

  auto EnsurePublishResources() -> bool;

  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  EnvironmentProbeState probe_state_ {};
  ProbeRefreshState last_probe_refresh_state_ {};
  PublicationState last_publication_state_ {};
  Stage15State last_stage15_state_ {};
  std::unique_ptr<internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>
    bindings_publisher_ {};
  std::unordered_map<ViewId, PublishedView> published_views_ {};
  std::unique_ptr<environment::SkyRenderer> sky_ {};
  std::unique_ptr<environment::AtmosphereRenderer> atmosphere_ {};
  std::unique_ptr<environment::FogRenderer> fog_ {};
  std::unique_ptr<environment::internal::IblProcessor> ibl_ {};
};

} // namespace oxygen::vortex

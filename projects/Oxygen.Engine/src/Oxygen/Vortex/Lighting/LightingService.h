//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace lighting {
class DeferredLightPass;
namespace internal {
class DeferredLightPacketBuilder;
class ForwardLightPublisher;
class LightGridBuilder;
} // namespace internal
} // namespace lighting

class LightingService {
public:
  struct GridBuildState {
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    std::uint32_t build_count { 0U };
    std::uint32_t published_view_count { 0U };
    std::uint32_t directional_light_count { 0U };
    std::uint32_t local_light_count { 0U };
    std::uint64_t selection_epoch { 0U };
  };

  struct DeferredLightingState {
    bool consumed_packets { false };
    bool accumulated_into_scene_color { false };
    bool used_service_owned_geometry { false };
    std::uint32_t directional_draw_count { 0U };
    std::uint32_t local_light_draw_count { 0U };
    std::uint64_t selection_epoch { 0U };
  };

  OXGN_VRTX_API explicit LightingService(Renderer& renderer);
  OXGN_VRTX_API ~LightingService();

  LightingService(const LightingService&) = delete;
  auto operator=(const LightingService&) -> LightingService& = delete;
  LightingService(LightingService&&) = delete;
  auto operator=(LightingService&&) -> LightingService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto BuildLightGrid(const FrameLightingInputs& inputs) -> void;
  OXGN_VRTX_API auto RenderDeferredLighting(RenderContext& ctx,
    const SceneTextures& scene_textures,
    const FrameLightSelection& frame_light_set) -> void;

  [[nodiscard]] OXGN_VRTX_API auto InspectForwardLightBindings(ViewId view_id) const
    -> const LightingFrameBindings*;
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastGridBuildState() const noexcept
    -> const GridBuildState&
  {
    return last_grid_build_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastDeferredLightingState() const noexcept
    -> const DeferredLightingState&
  {
    return last_deferred_lighting_state_;
  }

private:
  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  GridBuildState last_grid_build_state_ {};
  DeferredLightingState last_deferred_lighting_state_ {};
  std::unique_ptr<lighting::internal::LightGridBuilder> light_grid_builder_;
  std::unique_ptr<lighting::internal::ForwardLightPublisher> publisher_;
  std::unique_ptr<lighting::internal::DeferredLightPacketBuilder>
    deferred_packets_;
  std::unique_ptr<lighting::DeferredLightPass> deferred_pass_;
};

} // namespace oxygen::vortex

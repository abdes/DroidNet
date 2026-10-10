//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
}

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
struct SceneTexturesConfig;
class Renderer;
class SceneTextures;
class DepthPrepassMeshProcessor;
namespace occlusion::internal {
  class IndirectListBuilder;
} // namespace occlusion::internal

struct DepthPrepassConfig {
  DepthPrePassMode mode { DepthPrePassMode::kOpaqueAndMasked };
  bool write_velocity { true };
};

class DepthPrepassModule {
public:
  OXGN_VRTX_API explicit DepthPrepassModule(
    Renderer& renderer, const SceneTexturesConfig& scene_textures_config);
  OXGN_VRTX_API ~DepthPrepassModule();

  DepthPrepassModule(const DepthPrepassModule&) = delete;
  auto operator=(const DepthPrepassModule&) -> DepthPrepassModule& = delete;
  DepthPrepassModule(DepthPrepassModule&&) = delete;
  auto operator=(DepthPrepassModule&&) -> DepthPrepassModule& = delete;

  //! Clears the scene depth and draws the current view's phase 1 draws.
  OXGN_VRTX_API void ExecutePhase1(RenderContext& ctx,
    graphics::CommandRecorder& recorder, SceneTextures& scene_textures);
  //! Draws the phase 2 draws, when occlusion ran phase 2, and publishes the
  //! complete depth.
  OXGN_VRTX_API void ExecutePhase2(RenderContext& ctx,
    graphics::CommandRecorder& recorder, SceneTextures& scene_textures);
  OXGN_VRTX_API void SetConfig(const DepthPrepassConfig& config);

  [[nodiscard]] OXGN_VRTX_API auto GetCompleteness() const
    -> DepthPrePassCompleteness;
  [[nodiscard]] OXGN_VRTX_API auto HasValidDepthProduct() const -> bool;
  [[nodiscard]] OXGN_VRTX_API auto HasPublishedDepthProducts() const -> bool;
  //! Phase 1 drew into the scene depth and phase 2 has not run yet.
  [[nodiscard]] OXGN_VRTX_API auto HasPendingPhase2() const -> bool;

private:
  void DrawList(const RenderContext& ctx, graphics::CommandRecorder& recorder,
    SceneTextures& scene_textures, DrawVisibilityPredicate predicate);

  Renderer& renderer_;
  DepthPrepassConfig config_ {};
  DepthPrePassCompleteness completeness_ {
    DepthPrePassCompleteness::kDisabled,
  };
  bool has_valid_depth_product_ { false };
  bool has_published_depth_products_ { false };
  //! Phase 1 state that phase 2 continues.
  bool phase1_recorded_ { false };
  bool has_current_view_payload_ { false };
  bool writes_velocity_ { false };
  std::unique_ptr<DepthPrepassMeshProcessor> mesh_processor_;
  std::unique_ptr<occlusion::internal::IndirectListBuilder> list_builder_;
  std::shared_ptr<graphics::Framebuffer> depth_framebuffer_;
  std::shared_ptr<graphics::Framebuffer> depth_velocity_framebuffer_;
};

} // namespace oxygen::vortex

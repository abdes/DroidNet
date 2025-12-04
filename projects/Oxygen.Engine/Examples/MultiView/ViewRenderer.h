//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {
class DepthPrePass;
struct DepthPrePassConfig;
class ShaderPass;
struct ShaderPassConfig;
}

namespace oxygen::examples::multiview {

//! Per-view renderer that manages render passes for a single view.
/*!
 Encapsulates the render pass execution for one view (main or PiP).
 Uses lambda-based render graphs for Phase 2 architecture.
*/
class ViewRenderer {
public:
  struct Config {
    std::shared_ptr<graphics::Texture> color_texture;
    std::shared_ptr<graphics::Texture> depth_texture;
    graphics::Color clear_color { 0.1f, 0.2f, 0.38f, 1.0f };
    bool wireframe { false };
  };

  ViewRenderer() = default;
  ~ViewRenderer() = default;

  ViewRenderer(const ViewRenderer&) = delete;
  ViewRenderer& operator=(const ViewRenderer&) = delete;
  ViewRenderer(ViewRenderer&&) = default;
  ViewRenderer& operator=(ViewRenderer&&) = default;

  //! Configure the renderer with textures and settings.
  auto Configure(const Config& config) -> void;

  //! Reset any configuration and clear persistent texture references.
  /*!
    This forcefully clears the configured state so the renderer will be
    re-configured on the next Configure() call (e.g. after textures are
    recreated due to a resize).
  */
  auto ResetConfiguration() -> void;

  //! Execute render passes for this view.
  /*!
   Renders depth pre-pass and color pass to the configured textures.
   \param ctx RenderContext with scene data
   \param recorder CommandRecorder for GPU commands
   \param framebuffer Framebuffer containing color and depth attachments
  */
  auto Render(const engine::RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> co::Co<>;

  [[nodiscard]] auto IsConfigured() const -> bool
  {
    return config_.has_value();
  }

private:
  auto RenderDepthPrePass(const engine::RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> co::Co<>;

  auto RenderColorPass(const engine::RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> co::Co<>;

  std::optional<Config> config_;

  // Persistent passes (created once, reused every frame)
  std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config_;
  std::shared_ptr<engine::DepthPrePass> depth_pass_;
  std::shared_ptr<engine::ShaderPassConfig> shader_pass_config_;
  std::shared_ptr<engine::ShaderPass> shader_pass_;
};

} // namespace oxygen::examples::multiview

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "../Common/AsyncEngineApp.h"
#include "../Common/ExampleModuleBase.h"

namespace oxygen {
class Graphics;
}

namespace oxygen::examples::multiview {

//! Multi-view rendering example demonstrating Phase 2 features.
/*!
 This example showcases multi-view rendering with:
 - Main view: Full-screen solid-shaded sphere
 - PiP view: Top-right corner (25% size)

 Both views render the same scene with different cameras.
 Demonstrates PrepareView/RenderView APIs and per-view state isolation.
*/
class MainModule final : public common::ExampleModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = ExampleModuleBase;

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  explicit MainModule(const common::AsyncEngineApp& app) noexcept;
  ~MainModule() override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MultiViewExample";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return engine::ModulePriority { 500 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override;

  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameGraph(engine::FrameContext& context) -> co::Co<> override;
  auto OnCommandRecord(engine::FrameContext& context) -> co::Co<> override;

protected:
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

private:
  auto EnsureScene() -> void;
  auto EnsureMainCamera(int width, int height) -> void;
  auto EnsurePipCamera(int pip_width, int pip_height) -> void;
  auto ReleaseMainViewResources() -> void;
  auto ReleasePipViewResources() -> void;
  auto AcquireCommandRecorder(Graphics& gfx)
    -> std::shared_ptr<graphics::CommandRecorder>;
  auto TrackSwapchainFramebuffer(graphics::CommandRecorder& recorder,
    const graphics::Framebuffer& framebuffer) -> void;
  auto EnsureMainRenderTargets(Graphics& gfx, const graphics::Surface& surface,
    const graphics::Framebuffer& framebuffer,
    graphics::CommandRecorder& recorder) -> void;
  auto EnsurePipRenderTargets(Graphics& gfx, const graphics::Surface& surface,
    const graphics::Framebuffer& framebuffer,
    graphics::CommandRecorder& recorder) -> void;
  auto RenderMainViewOffscreen(engine::FrameContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;
  auto RenderPipViewWireframe(engine::FrameContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;
  auto CompositeMainViewToBackbuffer(engine::FrameContext& context,
    graphics::CommandRecorder& recorder,
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const graphics::Surface& surface) -> void;
  auto CompositePipViewToBackbuffer(graphics::CommandRecorder& recorder,
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const graphics::Surface& surface) -> void;
  auto MarkSurfacePresentable(engine::FrameContext& context,
    const std::shared_ptr<graphics::Surface>& surface) -> void;
  auto ReleaseTexture(std::shared_ptr<graphics::Texture>& texture) -> void;
  auto ReleaseFramebuffer(std::shared_ptr<graphics::Framebuffer>& framebuffer)
    -> void;

  const common::AsyncEngineApp& app_;
  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode main_camera_;
  scene::SceneNode pip_camera_;
  scene::SceneNode sphere_node_;

  // Multi-view state (Phase 2 pattern)
  std::shared_ptr<renderer::CameraView> main_camera_view_;
  ViewId main_view_id_ {};
  std::shared_ptr<renderer::CameraView> pip_camera_view_;
  ViewId pip_view_id_ {};

  // Main view resources (separate render targets)
  std::shared_ptr<graphics::Texture> main_depth_texture_;
  std::shared_ptr<graphics::Texture> main_color_texture_;
  std::shared_ptr<graphics::Framebuffer> main_framebuffer_;
  bool main_view_ready_ { false };

  // PiP resources
  std::shared_ptr<graphics::Texture> pip_depth_texture_;
  std::shared_ptr<graphics::Texture> pip_color_texture_;
  std::shared_ptr<graphics::Framebuffer> pip_framebuffer_;
  uint32_t pip_target_width_ { 0 };
  uint32_t pip_target_height_ { 0 };
  bool pip_view_ready_ { false };

  std::optional<ViewPort> pip_destination_viewport_ {};

  bool initialized_ { false };
};

} // namespace oxygen::examples::multiview

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>

#include "MultiView/DemoView.h"

namespace oxygen::engine {
class Renderer;
struct RenderContext;
} // namespace oxygen::engine

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::examples::multiview {

class ImGuiView final : public DemoView {
public:
  ImGuiView();
  ~ImGuiView() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ImGuiView)
  OXYGEN_DEFAULT_MOVABLE(ImGuiView)

  void SetImGui(observer_ptr<imgui::ImGuiModule> module)
  {
    imgui_module_ = module;
  }

  void Initialize(scene::Scene& scene) override;

  void OnSceneMutation() override;

  // Override to prevent default ViewRenderer registration
  void RegisterViewForRendering(engine::Renderer& renderer) override;

  auto OnPreRender(engine::Renderer& renderer) -> co::Co<> override;

  auto RenderFrame(const engine::RenderContext& render_ctx,
    graphics::CommandRecorder& recorder) -> co::Co<> override;

  void Composite(graphics::CommandRecorder& recorder,
    graphics::Texture& backbuffer) override;

protected:
  void OnReleaseResources() override;

private:
  //! Updates the view context to match the current surface size.
  void UpdateViewForCurrentSurface();

  void EnsureImGuiRenderTargets();

  uint32_t last_surface_width_ { 0 };
  uint32_t last_surface_height_ { 0 };

  observer_ptr<imgui::ImGuiModule> imgui_module_ { nullptr };
};

} // namespace oxygen::examples::multiview

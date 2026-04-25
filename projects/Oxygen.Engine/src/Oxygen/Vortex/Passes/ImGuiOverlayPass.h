//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class Texture;
namespace imgui {
  class ImGuiGraphicsBackend;
} // namespace imgui
} // namespace oxygen::graphics

namespace oxygen::vortex {

class Renderer;

class ImGuiOverlayPass {
public:
  struct Inputs {
    observer_ptr<graphics::imgui::ImGuiGraphicsBackend> backend { nullptr };
    observer_ptr<const graphics::Framebuffer> target { nullptr };
    observer_ptr<const graphics::Texture> color_texture { nullptr };
  };

  OXGN_VRTX_API explicit ImGuiOverlayPass(Renderer& renderer);
  OXGN_VRTX_API ~ImGuiOverlayPass();

  ImGuiOverlayPass(const ImGuiOverlayPass&) = delete;
  auto operator=(const ImGuiOverlayPass&) -> ImGuiOverlayPass& = delete;
  ImGuiOverlayPass(ImGuiOverlayPass&&) = delete;
  auto operator=(ImGuiOverlayPass&&) -> ImGuiOverlayPass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Record(const Inputs& inputs) const -> bool;

private:
  Renderer& renderer_;
};

} // namespace oxygen::vortex

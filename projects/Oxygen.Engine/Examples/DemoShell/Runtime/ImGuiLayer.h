//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Vortex/SurfaceLayerSource.h>

namespace oxygen::examples {

//! Draws the ImGui module's frame as a Vortex surface layer.
class ImGuiLayer final : public vortex::SurfaceLayerSource {
public:
  explicit ImGuiLayer(observer_ptr<imgui::ImGuiModule> module) noexcept
    : module_(module)
  {
  }

  [[nodiscard]] auto IsActive() const noexcept -> bool override
  {
    return module_->IsFrameActive();
  }

  auto Record(graphics::CommandRecorder& recorder) -> void override
  {
    module_->Render(recorder);
  }

private:
  observer_ptr<imgui::ImGuiModule> module_;
};

} // namespace oxygen::examples

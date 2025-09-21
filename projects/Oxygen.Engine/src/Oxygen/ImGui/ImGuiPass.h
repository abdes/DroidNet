//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/ImGui/api_export.h>
#include <Oxygen/Imgui/ImGuiGraphicsBackend.h>
#include <Oxygen/OxCo/Co.h>

struct ImDrawData;

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::imgui {

class ImGuiModule;

//! Simple ImGui renderer that just calls the backend.
/*!
 This class is a minimal wrapper around ImGuiGraphicsBackend. It doesn't follow
 the engine's bindless rendering model since ImGui uses its own rendering
 pipeline via imgui_impl_dx12.
*/
class ImGuiPass final {
public:
  OXGN_IMGUI_API explicit ImGuiPass(
    std::shared_ptr<ImGuiGraphicsBackend> backend);

  OXYGEN_MAKE_NON_COPYABLE(ImGuiPass)
  OXYGEN_MAKE_NON_MOVABLE(ImGuiPass)

  ~ImGuiPass() = default;

  //! Render ImGui using the provided command recorder.
  OXGN_IMGUI_API auto Render(graphics::CommandRecorder& recorder) const
    -> co::Co<>;

private:
  friend class ImGuiModule;
  auto Disable() -> void { disabled_ = true; }
  auto Enable() -> void { disabled_ = false; }
  // ImGui pass is disabled by default until the ImGuiModule has all valid
  // prerequisites for ImGui rendering (e.g. valid window). It will be disabled
  // anytime such prerequisites are lost.
  bool disabled_ { true };

  std::shared_ptr<ImGuiGraphicsBackend> backend_ {};
};

} // namespace oxygen::imgui

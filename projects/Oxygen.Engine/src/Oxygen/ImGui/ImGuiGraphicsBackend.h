//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>

struct ImDrawData;
struct ImGuiContext;

namespace oxygen {
class Graphics;
namespace graphics {
  class CommandRecorder;
} // namespace graphics
} // namespace oxygen

namespace oxygen::imgui {

//! Minimal abstract interface that graphics backends should implement to
//! render ImGui draw data.
/*!
 Allows for the ImGui backend to be specialized along with the Graphics backend.
 Implementations (e.g. D3D12) will live in the graphics backend module due to
 tight coupling.

 The lifecycle of the ImGui backend follows the lifecycle of the `ImGuiModule`
 itself, and not the underlying Graphics backend.
*/
class ImGuiGraphicsBackend {
public:
  ImGuiGraphicsBackend() = default;

  OXYGEN_MAKE_NON_COPYABLE(ImGuiGraphicsBackend)
  OXYGEN_MAKE_NON_MOVABLE(ImGuiGraphicsBackend)

  virtual ~ImGuiGraphicsBackend() = default;

  [[nodiscard]] virtual auto GetName() const noexcept -> std::string_view = 0;

  //! Initialize backend with the engine Graphics pointer. Called by the
  //! module when a Graphics instance becomes available.
  virtual auto Init(std::weak_ptr<Graphics> gfx) -> void = 0;

  //! Shutdown and release any GPU resources owned by the backend.
  virtual auto Shutdown() -> void = 0;

  //! Called once per-frame before ImGui operations. Backend handles
  //! ImGui context setup and calls ImGui::NewFrame().
  virtual auto NewFrame() -> void = 0;

  //! Record ImGui draw commands into an existing command recorder or command
  //! list provided by the renderer. Implementations must not create or submit
  //! command lists; they should only encode GPU commands using the provided
  //! recorder/list. Backend gets draw data from ImGui internally.
  virtual auto Render(graphics::CommandRecorder& recorder) -> void = 0;

  // FIXME: Temporarily, the ImGui context is unique and owned by the backend
  virtual auto GetImGuiContext() -> ImGuiContext* = 0;

  //! Notify the backend that device/swapchain related objects have changed and
  //! device-local resources must be recreated. Default implementation is a
  //! no-op; backends which allocate device objects may override this to
  //! invalidate/re-create resources after a swapchain resize or device reset.
  /*! @note Adding a default no-op keeps this change backward compatible for
      any existing backend implementations. */
  virtual auto RecreateDeviceObjects() -> void { /* no-op */ };
};

} // namespace oxygen::imgui

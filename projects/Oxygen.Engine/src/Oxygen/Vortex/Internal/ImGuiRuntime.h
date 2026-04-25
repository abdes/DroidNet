//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Platform/Types.h>

struct ImGuiContext;

namespace oxygen {
class Graphics;
class Platform;
namespace graphics {
class Framebuffer;
class Texture;
namespace imgui {
  class ImGuiGraphicsBackend;
} // namespace imgui
} // namespace graphics
namespace platform::imgui {
class ImGuiSdl3Backend;
} // namespace platform::imgui
namespace vortex {
class Renderer;
}
} // namespace oxygen

namespace oxygen::vortex::internal {

class ImGuiRuntime {
public:
  struct OverlayComposition {
    std::shared_ptr<graphics::Texture> texture {};
    ViewPort viewport {};
  };

  ImGuiRuntime(std::shared_ptr<Platform> platform,
    std::unique_ptr<graphics::imgui::ImGuiGraphicsBackend> graphics_backend);
  ~ImGuiRuntime();

  OXYGEN_MAKE_NON_COPYABLE(ImGuiRuntime)
  OXYGEN_MAKE_NON_MOVABLE(ImGuiRuntime)

  [[nodiscard]] auto Initialize(std::weak_ptr<Graphics> gfx) -> bool;
  auto Shutdown() noexcept -> void;

  auto SetWindowId(platform::WindowIdType window_id) -> void;
  auto OnFrameStart() -> void;
  auto OnFrameEnd() -> void;

  [[nodiscard]] auto IsFrameActive() const noexcept -> bool
  {
    return frame_started_;
  }

  [[nodiscard]] auto GetImGuiContext() const noexcept -> ImGuiContext*;

  [[nodiscard]] auto RenderOverlay(Renderer& renderer,
    observer_ptr<const graphics::Framebuffer> composite_target)
    -> std::optional<OverlayComposition>;

private:
  auto ApplyDefaultStyleAndFonts() -> void;
  [[nodiscard]] auto EnsureOverlayFramebuffer(
    observer_ptr<Graphics> gfx, std::uint32_t width, std::uint32_t height)
    -> bool;
  auto ClearOverlayFramebuffer() noexcept -> void;

  std::shared_ptr<Platform> platform_ {};
  std::shared_ptr<graphics::imgui::ImGuiGraphicsBackend> graphics_backend_ {};
  std::unique_ptr<platform::imgui::ImGuiSdl3Backend> platform_backend_ {};
  platform::WindowIdType window_id_ { platform::kInvalidWindowId };
  std::size_t platform_window_destroy_handler_token_ { 0U };
  bool initialized_ { false };
  bool frame_started_ { false };
  std::shared_ptr<graphics::Texture> overlay_texture_ {};
  std::shared_ptr<graphics::Framebuffer> overlay_framebuffer_ {};
  std::uint32_t overlay_width_ { 0U };
  std::uint32_t overlay_height_ { 0U };
};

} // namespace oxygen::vortex::internal

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <imgui.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Renderer/api_export.h>

struct ImGuiContext;
struct ImDrawData;

namespace oxygen {
namespace platform::imgui {
  class ImGuiSdl3Backend;
}
namespace graphics::imgui {
  class ImGuiGraphicsBackend;
}
namespace renderer::imgui {
  class ImGuiPass;
}
namespace engine {
  class FrameContext;
}
} // namespace oxygen

namespace oxygen::engine::imgui {

class ImGuiModule final : public engine::EngineModule {
  OXYGEN_TYPED(ImGuiModule)

public:
  using ModulePriority = engine::ModulePriority;

  OXGN_RNDR_API ImGuiModule(std::shared_ptr<Platform> platform,
    std::unique_ptr<graphics::imgui::ImGuiGraphicsBackend> graphics_backend);

  OXYGEN_MAKE_NON_COPYABLE(ImGuiModule)
  OXYGEN_MAKE_NON_MOVABLE(ImGuiModule)

  OXGN_RNDR_API ~ImGuiModule() override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "ImGuiModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    // TODO(abdes) consolidate predefined module priorities
    return engine::ModulePriority { 1000 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return ::oxygen::engine::MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kFrameEnd>();
  }

  // Lifecycle
  OXGN_RNDR_NDAPI auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool override;

  OXGN_RNDR_API auto OnShutdown() noexcept -> void override;

  OXGN_RNDR_API auto OnFrameStart(
    observer_ptr<engine::FrameContext> /*context*/) -> void override;

  // Ensure ImGui gets a matching EndFrame when NewFrame was started but
  // rendering did not happen for some reason (e.g. surface gone or pass
  // skipped). This prevents ImGui's sanity checks from asserting on the
  // next frame start.
  OXGN_RNDR_API auto OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
    -> void override;

  //! Access the owned render pass. Useful so other systems (for example the
  //! render-graph builder) can retrieve the configured pass and add it to the
  //! graph. The module retains ownership unless the pass is transferred.
  OXGN_RNDR_NDAPI auto GetRenderPass() const noexcept
    -> observer_ptr<renderer::imgui::ImGuiPass>;

  OXGN_RNDR_API auto SetWindowId(platform::WindowIdType window_id) -> void;

  //! Get the ImGui context managed by the graphics backend
  //! This allows other modules to set the context when making ImGui calls
  OXGN_RNDR_NDAPI auto GetImGuiContext() const noexcept -> ImGuiContext*;

  //! Request that the graphics backend re-create any device-local ImGui
  //! objects (called after swapchain/surface reconfiguration).
  OXGN_RNDR_API auto RecreateDeviceObjects() -> void;

  auto IsWitinFrameScope() const noexcept { return frame_started_; }

private:
  std::shared_ptr<Platform> platform_;
  std::unique_ptr<platform::imgui::ImGuiSdl3Backend> platform_backend_;
  // Store graphics backend as shared so we can share it with the ImGuiPass
  // while keeping ownership in the module.
  std::shared_ptr<graphics::imgui::ImGuiGraphicsBackend> graphics_backend_;

  // Store window ID for lazy platform backend creation
  platform::WindowIdType window_id_ { platform::kInvalidWindowId };

  // Token returned by Platform when we register for pre-destroy notifications
  // for the currently tracked window. Zero means no registration.
  size_t platform_window_destroy_handler_token_ { 0 };

  // Owned ImGuiPass instance created and configured by this module.
  std::unique_ptr<renderer::imgui::ImGuiPass> render_pass_;
  // Track whether we successfully started an ImGui frame (ImGui::NewFrame)
  // so we can EndFrame if a render path was skipped.
  bool frame_started_ { false };
};

} // namespace oxygen::engine::imgui

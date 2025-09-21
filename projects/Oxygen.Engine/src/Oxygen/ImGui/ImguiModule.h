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
#include <Oxygen/ImGui/api_export.h>
#include <Oxygen/Imgui/ImGuiGraphicsBackend.h>
#include <Oxygen/Imgui/ImGuiPass.h>
#include <Oxygen/Platform/Types.h>

struct ImGuiContext;
struct ImDrawData;

namespace oxygen::platform::imgui {
class ImGuiSdl3Backend;
} // namespace oxygen::platform::imgui

namespace oxygen::engine {
class FrameContext;
} // namespace oxygen::engine

namespace oxygen::imgui {

class ImGuiModule final : public engine::EngineModule {
  OXYGEN_TYPED(ImGuiModule)

public:
  using ModulePriority = engine::ModulePriority;

  OXGN_IMGUI_API ImGuiModule(std::shared_ptr<Platform> platform,
    std::unique_ptr<ImGuiGraphicsBackend> graphics_backend);

  OXYGEN_MAKE_NON_COPYABLE(ImGuiModule)
  OXYGEN_MAKE_NON_MOVABLE(ImGuiModule)

  OXGN_IMGUI_API ~ImGuiModule() override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "ImGuiModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    return engine::ModulePriority { 1000 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    using namespace oxygen::core;
    return ::oxygen::engine::MakeModuleMask<PhaseId::kFrameStart>();
  }

  // Lifecycle
  OXGN_IMGUI_NDAPI auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool override;

  OXGN_IMGUI_API auto OnShutdown() noexcept -> void override;

  OXGN_IMGUI_API auto OnFrameStart(engine::FrameContext& /*context*/)
    -> void override;

  //! Access the owned render pass. Useful so other systems (for example the
  //! render-graph builder) can retrieve the configured pass and add it to the
  //! graph. The module retains ownership unless the pass is transferred.
  OXGN_IMGUI_NDAPI auto GetRenderPass() const noexcept
    -> observer_ptr<ImGuiPass>;

  OXGN_IMGUI_API auto SetWindowId(platform::WindowIdType window_id) -> void;

  //! Get the ImGui context managed by the graphics backend
  //! This allows other modules to set the context when making ImGui calls
  OXGN_IMGUI_NDAPI auto GetImGuiContext() const noexcept -> ImGuiContext*;

private:
  std::shared_ptr<Platform> platform_ {};
  std::unique_ptr<platform::imgui::ImGuiSdl3Backend> platform_backend_ {};
  // Store graphics backend as shared so we can share it with the ImGuiPass
  // while keeping ownership in the module.
  std::shared_ptr<ImGuiGraphicsBackend> graphics_backend_ {};

  // Store window ID for lazy platform backend creation
  platform::WindowIdType window_id_ { platform::kInvalidWindowId };

  // Owned ImGuiPass instance created and configured by this module.
  std::unique_ptr<ImGuiPass> render_pass_ {};
};

} // namespace oxygen::imgui

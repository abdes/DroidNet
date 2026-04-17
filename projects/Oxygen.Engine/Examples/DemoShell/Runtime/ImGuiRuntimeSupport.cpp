//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Graphics/Direct3D12/ImGui/ImGuiBackend.h>
#include <Oxygen/Renderer/ImGui/ImGuiModule.h>

#include "DemoShell/Runtime/ImGuiRuntimeSupport.h"

namespace oxygen::examples {

auto CreateImGuiRuntimeModule(const std::shared_ptr<Platform>& platform)
  -> std::unique_ptr<engine::EngineModule>
{
  auto graphics_backend
    = std::make_unique<graphics::d3d12::D3D12ImGuiGraphicsBackend>();
  return std::make_unique<engine::imgui::ImGuiModule>(
    platform, std::move(graphics_backend));
}

auto AttachImGuiWindow(
  observer_ptr<AsyncEngine> engine, const platform::WindowIdType window_id)
  -> bool
{
  DCHECK_NOTNULL_F(engine);

  auto imgui_module_ref = engine->GetModule<engine::imgui::ImGuiModule>();
  if (!imgui_module_ref) {
    LOG_F(INFO, "ImGui module not available; cannot bind to window {}",
      window_id);
    return false;
  }

  imgui_module_ref->get().SetWindowId(window_id);
  return true;
}

auto DetachImGuiWindow(observer_ptr<AsyncEngine> engine) noexcept -> void
{
  if (engine == nullptr) {
    return;
  }

  auto imgui_module_ref = engine->GetModule<engine::imgui::ImGuiModule>();
  if (!imgui_module_ref) {
    DLOG_F(INFO, "ImGui module not available; skipping window detach");
    return;
  }

  try {
    imgui_module_ref->get().SetWindowId(platform::kInvalidWindowId);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to unhook ImGui from window: {}", e.what());
  }
}

auto IsImGuiRuntimeModuleEvent(const engine::ModuleEvent& event) noexcept -> bool
{
  return event.type_id == engine::imgui::ImGuiModule::ClassTypeId();
}

} // namespace oxygen::examples

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Imgui/ImguiModule.h"

#include <imgui.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Core/Engine.h"
#include "Oxygen/Imgui/ImGuiPlatformBackend.h"
#include "Oxygen/platform/platform.h"
#include "Oxygen/Renderers/Common/CommandList.h"

using namespace oxygen::imgui;

ImguiModule::~ImguiModule() = default;

void ImguiModule::OnInitialize(const Renderer* renderer)
{
  imgui_platform_ = GetEngine().GetPlatform().CreateImGuiBackend(window_id_);
  if (!imgui_platform_) {
    LOG_F(ERROR, "Failed to create ImGui platform backend.");
    return;
  }

  IMGUI_CHECKVERSION();
  imgui_context_ = ImGui::CreateContext();
  ImGui::StyleColorsDark();

  imgui_platform_->Initialize(imgui_context_);
  ImGuiBackendInit(renderer);

  LOG_F(INFO, "[{}] initialized with `{}`", ObjectName(), imgui_platform_->ObjectName());
}

void ImguiModule::OnShutdown()
{
  ImGuiBackendShutdown();
  imgui_platform_->Shutdown();
  ImGui::DestroyContext();
}

auto ImguiModule::NewFrame(const Renderer* renderer) -> void
{
  ImGuiBackendNewFrame();
  imgui_platform_->NewFrame();
  ImGui::NewFrame();
}

auto ImguiModule::ImGuiRender(const Renderer* renderer) -> std::unique_ptr<renderer::CommandList>
{
  ImGui::Render();
  return ImGuiBackendRenderRawData(renderer, ImGui::GetDrawData());
}

void ImguiModule::ProcessInput(const platform::InputEvent& /*event*/)
{
  // Input is processed directly by the platform backend.
}

void ImguiModule::Update(Duration delta_time)
{
}

void ImguiModule::FixedUpdate()
{
}

auto ImguiModule::GetRenderInterface() -> ImGuiRenderInterface
{
  return ImGuiRenderInterface{ this };
}

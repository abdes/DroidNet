//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Imgui/ImguiModule.h"

#include <imgui.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Core/Engine.h"
#include "Oxygen/Graphics/Common/CommandList.h"
#include "Oxygen/Graphics/Common/Graphics.h"
#include "Oxygen/Imgui/ImGuiPlatformBackend.h"
#include "Oxygen/Platform/Platform.h"
#include "SDL/ImGuiSdl3Backend.h"

using oxygen::imgui::ImguiModule;

ImguiModule::~ImguiModule()
{
}

void ImguiModule::OnInitialize(const Graphics* gfx)
{
    DCHECK_NOTNULL_F(gfx);
    DCHECK_F(!gfx->IsWithoutRenderer());
    DCHECK_NOTNULL_F(gfx->GetRenderer());

    // TODO: FIXME Implement this
    // imgui_platform_ = GetEngine().GetPlatform().CreateImGuiBackend(window_id_);
    imgui_platform_ = std::make_shared<sdl3::ImGuiSdl3Backend>(nullptr, platform::kInvalidWindowId);
    if (!imgui_platform_) {
        LOG_F(ERROR, "Failed to create ImGui platform backend.");
        return;
    }

    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext();
    ImGui::StyleColorsDark();

    imgui_platform_->Initialize(imgui_context_);
    ImGuiBackendInit(gfx);

    LOG_F(INFO, "[{}] initialized with `{}`", Name(), imgui_platform_->ObjectName());
}

void ImguiModule::OnShutdown() noexcept
{
    try {
        ImGuiBackendShutdown();
        imgui_platform_->Shutdown();
        ImGui::DestroyContext();
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to shutdown ImGui module: {}", e.what());
    }
}

auto ImguiModule::NewFrame(const Graphics* gfx) -> void
{
    DCHECK_NOTNULL_F(gfx);
    DCHECK_NOTNULL_F(imgui_context_);

    ImGuiBackendNewFrame();
    imgui_platform_->NewFrame();
    ImGui::NewFrame();
}

auto ImguiModule::ImGuiRender(const Graphics* gfx) -> std::unique_ptr<graphics::CommandList>
{
    DCHECK_NOTNULL_F(gfx);
    DCHECK_NOTNULL_F(imgui_context_);

    ImGui::Render();
    return ImGuiBackendRenderRawData(gfx, ImGui::GetDrawData());
}

void ImguiModule::ProcessInput(const platform::InputEvent& /*event*/)
{
    DCHECK_NOTNULL_F(imgui_context_);
    // Input is processed directly by the platform backend.
}

void ImguiModule::Update(Duration /*delta_time*/)
{
    DCHECK_NOTNULL_F(imgui_context_);
}

void ImguiModule::FixedUpdate()
{
    DCHECK_NOTNULL_F(imgui_context_);
}

auto ImguiModule::GetRenderInterface() -> ImGuiRenderInterface
{
    return ImGuiRenderInterface { this };
}

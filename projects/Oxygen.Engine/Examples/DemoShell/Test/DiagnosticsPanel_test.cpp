//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>

#include <imgui.h>

#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/DiagnosticsPanel.h"
#include "DemoShell/UI/DiagnosticsVm.h"

namespace oxygen::examples::testing {

namespace {

  using oxygen::Graphics;
  using oxygen::RendererConfig;
  using oxygen::vortex::Renderer;
  using oxygen::vortex::RendererCapabilityFamily;
  using oxygen::vortex::testing::FakeGraphics;

  auto DestroyRenderer(Renderer* renderer) -> void
  {
    if (renderer != nullptr) {
      renderer->OnShutdown();
      std::default_delete<Renderer> {}(renderer);
    }
  }

  auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics)
    -> std::shared_ptr<Renderer>
  {
    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics).get();

    constexpr auto kCapabilities = RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kGpuUploadAndAssetBinding
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kShadowing
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition
      | RendererCapabilityFamily::kDiagnosticsAndProfiling;

    return { new Renderer(std::weak_ptr<Graphics>(graphics), std::move(config),
               kCapabilities),
      DestroyRenderer };
  }

  class DiagnosticsPanelTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      ResetDemoSettings();
      imgui_context_ = ImGui::CreateContext();
      ImGui::SetCurrentContext(imgui_context_);
      auto& io = ImGui::GetIO();
      io.DisplaySize = ImVec2 { 1280.0F, 720.0F };
      io.DeltaTime = 1.0F / 60.0F;
      io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }

    void TearDown() override
    {
      if (imgui_context_ != nullptr) {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
      }
      ResetDemoSettings();
    }

    static auto ResetDemoSettings() -> void
    {
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      std::error_code ec;
      std::filesystem::remove(settings->GetStoragePath(), ec);
      settings->Load();
    }

    ImGuiContext* imgui_context_ { nullptr };
  };

} // namespace

NOLINT_TEST_F(DiagnosticsPanelTest, DrawContentsWithVortexRendererBound)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  auto renderer = MakeRenderer(graphics);

  RenderingSettingsService settings_service {};
  settings_service.BindVortexRenderer(observer_ptr { renderer.get() });
  ui::DiagnosticsVm vm { observer_ptr { &settings_service } };
  ui::DiagnosticsPanel panel { observer_ptr { &vm } };

  EXPECT_EQ(panel.GetName(), "Diagnostics");

  auto& io = ImGui::GetIO();
  io.DisplaySize = ImVec2 { 1280.0F, 720.0F };
  io.DeltaTime = 1.0F / 60.0F;

  ImGui::NewFrame();
  panel.DrawContents();
  ImGui::Render();

  EXPECT_EQ(settings_service.GetDebugMode(), engine::ShaderDebugMode::kDisabled);
  EXPECT_EQ(
    settings_service.GetEffectiveDebugMode(), engine::ShaderDebugMode::kDisabled);
}

} // namespace oxygen::examples::testing

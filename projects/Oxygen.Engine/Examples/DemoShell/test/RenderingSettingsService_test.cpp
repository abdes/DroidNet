//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/test/Fakes/Graphics.h>

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
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting;
    return { new Renderer(std::weak_ptr<Graphics>(graphics), std::move(config),
               kCapabilities),
      DestroyRenderer };
  }

  class RenderingSettingsServiceTest : public ::testing::Test {
  protected:
    void SetUp() override { ResetDemoSettings(); }
    void TearDown() override { ResetDemoSettings(); }

    static auto ResetDemoSettings() -> void
    {
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      std::error_code ec;
      std::filesystem::remove(settings->GetStoragePath(), ec);
      settings->Load();
    }

    RenderingSettingsService service_;
  };

} // namespace

NOLINT_TEST_F(RenderingSettingsServiceTest, WireframeColorAppliesToRenderer)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  auto renderer = MakeRenderer(graphics);
  service_.BindVortexRenderer(observer_ptr { renderer.get() });

  service_.SetWireframeColor(
    oxygen::graphics::Color { 2.5F, 0.5F, 4.0F, 1.0F });

  EXPECT_FLOAT_EQ(renderer->GetWireframeColor().r, 2.5F);
  EXPECT_FLOAT_EQ(renderer->GetWireframeColor().g, 0.5F);
  EXPECT_FLOAT_EQ(renderer->GetWireframeColor().b, 4.0F);
}

//! The view mode belongs to the host toolbar: the service never sets the
//! renderer-wide render or debug mode.
NOLINT_TEST_F(RenderingSettingsServiceTest, LeavesRendererViewModeAlone)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  auto renderer = MakeRenderer(graphics);
  renderer->SetRenderMode(vortex::RenderMode::kWireframe);
  renderer->SetShaderDebugMode(vortex::ShaderDebugMode::kDirectLightingOnly);

  service_.BindVortexRenderer(observer_ptr { renderer.get() });
  service_.SetGpuDebugPassEnabled(true);
  service_.OnFrameStart(engine::FrameContext {});

  EXPECT_EQ(renderer->GetRenderMode(), vortex::RenderMode::kWireframe);
  EXPECT_EQ(renderer->GetShaderDebugMode(),
    vortex::ShaderDebugMode::kDirectLightingOnly);
}

NOLINT_TEST_F(RenderingSettingsServiceTest, GpuDebugPassPersists)
{
  service_.SetGpuDebugPassEnabled(true);

  RenderingSettingsService reloaded {};
  EXPECT_TRUE(reloaded.GetGpuDebugPassEnabled());
}

} // namespace oxygen::examples::testing

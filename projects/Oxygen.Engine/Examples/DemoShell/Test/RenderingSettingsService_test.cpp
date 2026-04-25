//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

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
    constexpr auto kCapabilities
      = RendererCapabilityFamily::kScenePreparation
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

    RenderingSettingsService service_ {};
  };

} // namespace

NOLINT_TEST_F(RenderingSettingsServiceTest,
  ShadowQualityTier_DefaultsToUltraForBackwardCompatibility)
{
  EXPECT_EQ(service_.GetShadowQualityTier(), ShadowQualityTier::kUltra);
}

NOLINT_TEST_F(RenderingSettingsServiceTest,
  ShadowQualityTier_PersistsReadableStringValues)
{
  service_.SetShadowQualityTier(ShadowQualityTier::kMedium);

  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  ASSERT_TRUE(settings->GetString("rendering.shadow_quality_tier").has_value());
  EXPECT_EQ(*settings->GetString("rendering.shadow_quality_tier"), "medium");
  EXPECT_EQ(service_.GetShadowQualityTier(), ShadowQualityTier::kMedium);
}

NOLINT_TEST_F(RenderingSettingsServiceTest,
  ShadowQualityTier_ReadsLegacyNumericPersistence)
{
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  settings->SetFloat("rendering.shadow_quality_tier", 1.0F);

  EXPECT_EQ(service_.GetShadowQualityTier(), ShadowQualityTier::kMedium);
}

NOLINT_TEST_F(RenderingSettingsServiceTest,
  VortexBoundDebugModePersistsDirectLightingOnly)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  auto renderer = MakeRenderer(graphics);
  service_.BindVortexRenderer(observer_ptr { renderer.get() });

  service_.SetDebugMode(engine::ShaderDebugMode::kDirectLightingOnly);

  EXPECT_TRUE(
    service_.SupportsDebugMode(engine::ShaderDebugMode::kDirectLightingOnly));
  EXPECT_EQ(
    service_.GetDebugMode(), engine::ShaderDebugMode::kDirectLightingOnly);
}

NOLINT_TEST_F(RenderingSettingsServiceTest,
  LightCullingDisabledDoesNotOverrideRenderingDebugModeOnVortex)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  auto renderer = MakeRenderer(graphics);

  LightCullingSettingsService light_culling_service {};
  service_.BindVortexRenderer(observer_ptr { renderer.get() });
  light_culling_service.BindVortexRenderer(observer_ptr { renderer.get() });

  service_.SetDebugMode(engine::ShaderDebugMode::kDirectLightingOnly);
  light_culling_service.SetVisualizationMode(engine::ShaderDebugMode::kDisabled);
  light_culling_service.OnMainViewReady(
    engine::FrameContext {}, vortex::CompositionView {});

  EXPECT_EQ(renderer->GetShaderDebugMode(),
    vortex::ShaderDebugMode::kDirectLightingOnly);
}

} // namespace oxygen::examples::testing

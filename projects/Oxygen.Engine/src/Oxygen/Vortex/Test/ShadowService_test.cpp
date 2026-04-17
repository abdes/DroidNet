//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include <glm/vec3.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::vortex::DirectionalShadowFrameData;
using oxygen::vortex::FrameDirectionalLightSelection;
using oxygen::vortex::FrameLightSelection;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::ShadowCascadeBinding;
using oxygen::vortex::ShadowFrameBindings;
using oxygen::vortex::ShadowService;
using oxygen::vortex::testing::FakeGraphics;

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  auto input = std::ifstream(path);
  EXPECT_TRUE(input.is_open()) << "failed to open " << path.generic_string();
  return {
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>(),
  };
}

auto SourceRoot() -> std::filesystem::path
{
  return std::filesystem::path { __FILE__ }.parent_path().parent_path()
    .parent_path();
}

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
    | RendererCapabilityFamily::kLightingData;
  return { new Renderer(
             std::weak_ptr<Graphics>(graphics), std::move(config), kCapabilities),
    DestroyRenderer };
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  ShadowFrameBindingsExposeDirectionalConventionalShadowContract)
{
  auto bindings = ShadowFrameBindings {};

  EXPECT_EQ(bindings.conventional_shadow_surface_handle,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.cascade_count, 0U);
  EXPECT_EQ(bindings.technique_flags, 0U);
  EXPECT_EQ(bindings.sampling_contract_flags, 0U);
  EXPECT_EQ(bindings.cascades.size(), ShadowFrameBindings::kMaxCascades);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  DirectionalShadowFrameDataDoesNotClaimLocalLightShadowPayload)
{
  auto frame_data = DirectionalShadowFrameData {};
  frame_data.bindings.cascade_count = 2U;

  EXPECT_EQ(frame_data.backing_resolution.x, 0U);
  EXPECT_EQ(frame_data.backing_resolution.y, 0U);
  EXPECT_EQ(frame_data.storage_flags, 0U);
  EXPECT_EQ(frame_data.bindings.cascade_count, 2U);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  FrameLightSelectionCarriesSharedDirectionalAuthorityForShadowService)
{
  auto selection = FrameLightSelection {};
  selection.selection_epoch = 19U;
  selection.directional_light = FrameDirectionalLightSelection {
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F },
    .color = glm::vec3 { 1.0F, 0.95F, 0.8F },
    .illuminance_lux = 1400.0F,
    .cascade_count = 4U,
  };

  ASSERT_TRUE(selection.directional_light.has_value());
  EXPECT_EQ(selection.directional_light->cascade_count, 4U);
  EXPECT_TRUE(selection.local_lights.empty());
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  ShadowServiceIsANonPlaceholderDirectionalOnlySubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<ShadowService>));
  EXPECT_TRUE((std::is_destructible_v<ShadowService>));
  EXPECT_TRUE((std::is_standard_layout_v<ShadowCascadeBinding>));
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  VortexModuleRegistersDirectionalShadowFamilySourcesOnly)
{
  const auto source_root = SourceRoot();
  const auto cmake_source = ReadTextFile(source_root / "Vortex/CMakeLists.txt");

  EXPECT_TRUE(cmake_source.contains("Shadows/ShadowService.h"));
  EXPECT_TRUE(cmake_source.contains("Shadows/ShadowService.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Shadows/Internal/CascadeShadowSetup.cpp"));
  EXPECT_TRUE(cmake_source.contains(
    "Shadows/Internal/ConventionalShadowTargetAllocator.cpp"));
  EXPECT_TRUE(cmake_source.contains("Shadows/Internal/ShadowCasterCulling.cpp"));
  EXPECT_TRUE(cmake_source.contains("Shadows/Passes/CascadeShadowPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Shadows/Passes/ShadowDepthPass.cpp"));
  EXPECT_FALSE(cmake_source.contains("Shadows/Vsm/Internal/"));
  EXPECT_FALSE(cmake_source.contains("ShadowLocalLights"));
}

class ShadowServiceBehaviorTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer_ = MakeRenderer(graphics_);
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  DirectionalOnlyShadowServiceStartsWithEmptyDirectionalPublicationAndNoVsm)
{
  auto service = ShadowService(*renderer_);

  EXPECT_FALSE(service.HasVsm());
  EXPECT_EQ(service.InspectShadowData(oxygen::ViewId { 11U }), nullptr);
}

} // namespace

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
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include <glm/vec3.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::ResolvedView;
using oxygen::ViewPort;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::vortex::DirectionalLightForwardData;
using oxygen::vortex::FrameDirectionalLightSelection;
using oxygen::vortex::FrameLightSelection;
using oxygen::vortex::FrameLightingInputs;
using oxygen::vortex::FrameLocalLightSelection;
using oxygen::vortex::LightingFrameBindings;
using oxygen::vortex::LightingService;
using oxygen::vortex::LocalLightKind;
using oxygen::vortex::PreparedViewLightingInput;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::testing::FakeGraphics;

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  auto input = std::ifstream(path);
  EXPECT_TRUE(input.is_open()) << "failed to open " << path.generic_string();
  return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
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

auto MakeResolvedView(const float width, const float height) -> ResolvedView
{
  auto params = ResolvedView::Params {};
  params.view_config.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  params.view_matrix = glm::mat4(1.0F);
  params.proj_matrix = glm::mat4(1.0F);
  params.near_plane = 0.1F;
  params.far_plane = 1000.0F;
  return ResolvedView(params);
}

NOLINT_TEST(LightingServiceSurfaceTest,
  LightingFrameBindingsExposeDirectionalAndClusterPublicationFields)
{
  auto bindings = LightingFrameBindings {};

  EXPECT_EQ(bindings.local_light_buffer_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.light_view_data_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.grid_metadata_buffer_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.grid_indirection_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.directional_light_indices_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.directional_light_count, 0U);
  EXPECT_EQ(bindings.local_light_count, 0U);
  EXPECT_EQ(bindings.has_directional_light, 0U);
  EXPECT_EQ(bindings.directional.cascade_count, 0U);
  EXPECT_EQ(bindings.directional.atmosphere_light_slot, 0xFFFFFFFFU);
  EXPECT_EQ(bindings.directional.atmosphere_mode_flags, 0U);
  EXPECT_EQ(
    bindings.directional.transmittance_toward_sun_rgb, glm::vec3(1.0F, 1.0F, 1.0F));
}

NOLINT_TEST(LightingServiceSurfaceTest,
  FrameLightSelectionCarriesSharedDirectionalAndLocalLightAuthority)
{
  auto selection = FrameLightSelection {};
  selection.selection_epoch = 77U;
  selection.directional_light = FrameDirectionalLightSelection {
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F },
    .color = glm::vec3 { 1.0F, 0.9F, 0.8F },
    .illuminance_lux = 1200.0F,
    .transmittance_toward_sun_rgb = glm::vec3 { 0.4F, 0.5F, 0.6F },
    .atmosphere_light_slot = 0U,
    .atmosphere_mode_flags
    = oxygen::vortex::kDirectionalLightAtmosphereModeFlagAuthority
      | oxygen::vortex::kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance,
  };
  selection.local_lights.push_back(FrameLocalLightSelection {
    .kind = LocalLightKind::kPoint,
    .position = glm::vec3 { 1.0F, 2.0F, 3.0F },
    .range = 6.0F,
    .color = glm::vec3 { 0.4F, 0.6F, 0.9F },
    .intensity = 80.0F,
  });

  ASSERT_TRUE(selection.directional_light.has_value());
  EXPECT_EQ(selection.selection_epoch, 77U);
  EXPECT_EQ(selection.local_lights.size(), 1U);
  EXPECT_EQ(selection.local_lights.front().kind, LocalLightKind::kPoint);
  EXPECT_EQ(selection.directional_light->illuminance_lux, 1200.0F);
  EXPECT_EQ(selection.directional_light->atmosphere_light_slot, 0U);
  EXPECT_EQ(selection.directional_light->transmittance_toward_sun_rgb,
    glm::vec3(0.4F, 0.5F, 0.6F));
}

NOLINT_TEST(LightingServiceSurfaceTest,
  LightingServiceIsANonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<oxygen::vortex::LightingService>));
  EXPECT_TRUE((std::is_destructible_v<oxygen::vortex::LightingService>));
  EXPECT_TRUE((std::is_standard_layout_v<DirectionalLightForwardData>));
}

NOLINT_TEST(LightingServiceSurfaceTest,
  VortexModuleRegistersLightingFamilySourcesAndSharedFrameAuthorityHeader)
{
  const auto source_root = SourceRoot();
  const auto cmake_source = ReadTextFile(source_root / "Vortex/CMakeLists.txt");

  EXPECT_TRUE(cmake_source.contains("Lighting/LightingService.h"));
  EXPECT_TRUE(cmake_source.contains("Lighting/LightingService.cpp"));
  EXPECT_TRUE(cmake_source.contains("Lighting/Internal/LightGridBuilder.h"));
  EXPECT_TRUE(cmake_source.contains("Lighting/Internal/ForwardLightPublisher.cpp"));
  EXPECT_TRUE(cmake_source.contains("Lighting/Internal/DeferredLightPacketBuilder.cpp"));
  EXPECT_TRUE(cmake_source.contains("Lighting/Passes/DeferredLightPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Types/FrameLightSelection.h"));
}

NOLINT_TEST(LightingServiceSurfaceTest,
  VortexOwnedDirectionalLightShaderHelpersLiveUnderVortexAndLegacyWrappersOnlyForward)
{
  const auto source_root = SourceRoot();
  const auto vortex_forward = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/ForwardDirectLighting.hlsli");
  const auto legacy_forward = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Forward/ForwardDirectLighting.hlsli");
  const auto vortex_atmosphere_helpers = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Lighting/AtmosphereLightingHelpers.hlsli");
  const auto legacy_atmosphere_helpers = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Renderer/AtmosphereLightingHelpers.hlsli");
  const auto vortex_lighting_contract = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Contracts/LightingFrameBindings.hlsli");
  const auto legacy_lighting_contract = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Renderer/LightingFrameBindings.hlsli");

  EXPECT_TRUE(vortex_forward.contains(
    "Vortex/Services/Lighting/AtmosphereDirectionalLightShared.hlsli"));
  EXPECT_TRUE(legacy_forward.contains(
    "Vortex/Services/Lighting/ForwardDirectLighting.hlsli"));
  EXPECT_TRUE(vortex_atmosphere_helpers.contains("ComputeSunTransmittance"));
  EXPECT_TRUE(legacy_atmosphere_helpers.contains(
    "Vortex/Services/Lighting/AtmosphereLightingHelpers.hlsli"));
  EXPECT_TRUE(vortex_lighting_contract.contains("DirectionalLightForwardData"));
  EXPECT_TRUE(legacy_lighting_contract.contains(
    "Vortex/Contracts/LightingFrameBindings.hlsli"));
}

class LightingServiceBehaviorTest : public ::testing::Test {
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

NOLINT_TEST_F(LightingServiceBehaviorTest,
  BuildLightGridPublishesSharedBuffersForEveryActiveViewOncePerFrame)
{
  auto service = LightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 8U }, oxygen::frame::Slot { 1U });

  auto selection = FrameLightSelection {};
  selection.selection_epoch = 91U;
  selection.directional_light = FrameDirectionalLightSelection {
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F },
    .source_radius = 0.05F,
    .color = glm::vec3 { 1.0F, 0.95F, 0.8F },
    .illuminance_lux = 1600.0F,
    .transmittance_toward_sun_rgb = glm::vec3 { 0.25F, 0.5F, 0.75F },
    .atmosphere_light_slot = 0U,
    .atmosphere_mode_flags
    = oxygen::vortex::kDirectionalLightAtmosphereModeFlagAuthority
      | oxygen::vortex::kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance,
  };
  selection.local_lights.push_back(FrameLocalLightSelection {
    .kind = LocalLightKind::kPoint,
    .position = glm::vec3 { 1.0F, 0.0F, 0.0F },
    .range = 6.0F,
    .color = glm::vec3 { 0.4F, 0.7F, 1.0F },
    .intensity = 80.0F,
  });
  selection.local_lights.push_back(FrameLocalLightSelection {
    .kind = LocalLightKind::kSpot,
    .position = glm::vec3 { -2.0F, 3.0F, 1.0F },
    .range = 8.0F,
    .color = glm::vec3 { 1.0F, 0.6F, 0.3F },
    .intensity = 120.0F,
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F },
    .decay_exponent = 2.0F,
    .inner_cone_cos = 0.95F,
    .outer_cone_cos = 0.75F,
  });

  auto first_view = MakeResolvedView(64.0F, 64.0F);
  auto second_view = MakeResolvedView(96.0F, 54.0F);
  const auto view_inputs = std::array {
    PreparedViewLightingInput {
      .view_id = oxygen::ViewId { 11U },
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr<const ResolvedView> { &first_view },
      .composition_view = {},
    },
    PreparedViewLightingInput {
      .view_id = oxygen::ViewId { 22U },
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr<const ResolvedView> { &second_view },
      .composition_view = {},
    },
  };

  service.BuildLightGrid(FrameLightingInputs {
    .frame_light_set = &selection,
    .active_views = std::span(view_inputs),
  });

  const auto& state = service.GetLastGridBuildState();
  EXPECT_EQ(state.build_count, 1U);
  EXPECT_EQ(state.published_view_count, 2U);
  EXPECT_EQ(state.directional_light_count, 1U);
  EXPECT_EQ(state.local_light_count, 2U);
  EXPECT_EQ(state.selection_epoch, 91U);

  const auto* first_bindings = service.InspectForwardLightBindings(oxygen::ViewId { 11U });
  const auto* second_bindings = service.InspectForwardLightBindings(oxygen::ViewId { 22U });
  ASSERT_NE(first_bindings, nullptr);
  ASSERT_NE(second_bindings, nullptr);
  EXPECT_NE(first_bindings->local_light_buffer_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(first_bindings->grid_metadata_buffer_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(first_bindings->grid_indirection_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(first_bindings->directional_light_indices_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(second_bindings->local_light_buffer_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(second_bindings->grid_metadata_buffer_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(second_bindings->grid_indirection_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(second_bindings->directional_light_indices_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    first_bindings->directional.transmittance_toward_sun_rgb, glm::vec3(0.25F, 0.5F, 0.75F));
  EXPECT_EQ(first_bindings->directional.atmosphere_light_slot, 0U);
  EXPECT_EQ(first_bindings->directional.atmosphere_mode_flags,
    oxygen::vortex::kDirectionalLightAtmosphereModeFlagAuthority
      | oxygen::vortex::kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance);
}

} // namespace

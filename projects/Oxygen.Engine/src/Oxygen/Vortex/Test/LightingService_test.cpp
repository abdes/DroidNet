//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>

#include <glm/vec3.hpp>

#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::vortex::DirectionalLightForwardData;
using oxygen::vortex::FrameDirectionalLightSelection;
using oxygen::vortex::FrameLightSelection;
using oxygen::vortex::FrameLocalLightSelection;
using oxygen::vortex::LightingFrameBindings;
using oxygen::vortex::LocalLightKind;

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

} // namespace

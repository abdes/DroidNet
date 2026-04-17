//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::vortex::EnvironmentAmbientBridgeBindings;
using oxygen::vortex::EnvironmentEvaluationParameters;
using oxygen::vortex::EnvironmentFrameBindings;
using oxygen::vortex::EnvironmentLightingService;
using oxygen::vortex::EnvironmentProbeBindings;
using oxygen::vortex::EnvironmentProbeState;

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

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentFrameBindingsExposePerViewProbeEvaluationAndBoundedBridgeState)
{
  const auto bindings = EnvironmentFrameBindings {};

  EXPECT_EQ(bindings.environment_static_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.environment_view_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.environment_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.prefiltered_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.probe_revision, 0U);
  EXPECT_FLOAT_EQ(bindings.evaluation.ambient_intensity, 1.0F);
  EXPECT_FLOAT_EQ(bindings.evaluation.average_brightness, 1.0F);
  EXPECT_FLOAT_EQ(bindings.evaluation.blend_fraction, 0.0F);
  EXPECT_EQ(bindings.evaluation.flags, 0U);
  EXPECT_EQ(bindings.ambient_bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bindings.ambient_bridge.ambient_intensity, 1.0F);
  EXPECT_FLOAT_EQ(bindings.ambient_bridge.average_brightness, 1.0F);
  EXPECT_FLOAT_EQ(bindings.ambient_bridge.blend_fraction, 0.0F);
  EXPECT_EQ(bindings.ambient_bridge.flags, 0U);
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  PersistentProbeStateRemainsSeparateFromPerViewEnvironmentPublication)
{
  const auto state = EnvironmentProbeState {};
  const auto bindings = EnvironmentFrameBindings {};

  EXPECT_FALSE(state.valid);
  EXPECT_EQ(state.flags, 0U);
  EXPECT_EQ(state.probes.environment_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.prefiltered_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.probe_revision, 0U);

  EXPECT_EQ(bindings.probes.probe_revision, state.probes.probe_revision);
  EXPECT_EQ(sizeof(EnvironmentProbeBindings), sizeof(bindings.probes));
  EXPECT_NE(static_cast<const void*>(&state.probes),
    static_cast<const void*>(&bindings.probes));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  AmbientBridgeBindingsStayNarrowAndDoNotIntroduceStage13PolicyFields)
{
  const auto bridge = EnvironmentAmbientBridgeBindings {};
  const auto evaluation = EnvironmentEvaluationParameters {};

  EXPECT_EQ(bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bridge.ambient_intensity, evaluation.ambient_intensity);
  EXPECT_FLOAT_EQ(bridge.average_brightness, evaluation.average_brightness);
  EXPECT_FLOAT_EQ(bridge.blend_fraction, evaluation.blend_fraction);
  EXPECT_EQ(bridge.flags, 0U);
  EXPECT_EQ(sizeof(EnvironmentAmbientBridgeBindings),
    2U * sizeof(std::uint32_t) + 3U * sizeof(float));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentLightingServiceIsConcreteNonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<EnvironmentLightingService>));
  EXPECT_TRUE((std::is_destructible_v<EnvironmentLightingService>));
  EXPECT_TRUE((std::is_standard_layout_v<EnvironmentProbeBindings>));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  VortexModuleRegistersEnvironmentServiceFamilySourcesAndHeaders)
{
  const auto source_root = SourceRoot();
  const auto cmake_source = ReadTextFile(source_root / "Vortex/CMakeLists.txt");

  EXPECT_TRUE(cmake_source.contains("Environment/EnvironmentLightingService.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/EnvironmentLightingService.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/SkyRenderer.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/AtmosphereRenderer.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/FogRenderer.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/IblProcessor.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/SkyPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/AtmosphereComposePass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/FogPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/IblProbePass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Types/EnvironmentProbeState.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Types/EnvironmentAmbientBridgeBindings.h"));
}

} // namespace

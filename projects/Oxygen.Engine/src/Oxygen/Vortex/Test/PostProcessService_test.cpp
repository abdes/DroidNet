//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>

#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::vortex::PostProcessConfig;
using oxygen::vortex::PostProcessFrameBindings;
using oxygen::vortex::PostProcessService;

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

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessFrameBindingsExposeStage22AuthoritySurface)
{
  const auto bindings = PostProcessFrameBindings {};

  EXPECT_EQ(bindings.resolved_scene_color_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.scene_depth_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.scene_velocity_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.bloom_texture_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.eye_adaptation_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.eye_adaptation_uav, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.post_history_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bindings.fixed_exposure, 1.0F);
  EXPECT_EQ(bindings.enable_bloom, 1U);
  EXPECT_EQ(bindings.enable_auto_exposure, 1U);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessConfigDefaultsRemainTonemapFirstAndFixedExposureSafe)
{
  const auto config = PostProcessConfig {};

  EXPECT_TRUE(config.enable_bloom);
  EXPECT_TRUE(config.enable_auto_exposure);
  EXPECT_FLOAT_EQ(config.fixed_exposure, 1.0F);
  EXPECT_FLOAT_EQ(config.bloom_intensity, 0.5F);
  EXPECT_FLOAT_EQ(config.bloom_threshold, 1.0F);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessServiceIsANonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<PostProcessService>));
  EXPECT_TRUE((std::is_destructible_v<PostProcessService>));
  EXPECT_TRUE((std::is_standard_layout_v<PostProcessFrameBindings>));
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  VortexModuleRegistersPostProcessFamilyAndFrameBindingSurface)
{
  const auto source_root = SourceRoot();
  const auto cmake_source = ReadTextFile(source_root / "Vortex/CMakeLists.txt");

  EXPECT_TRUE(cmake_source.contains("PostProcess/PostProcessService.h"));
  EXPECT_TRUE(cmake_source.contains("PostProcess/PostProcessService.cpp"));
  EXPECT_TRUE(cmake_source.contains("PostProcess/Internal/ExposureCalculator.cpp"));
  EXPECT_TRUE(cmake_source.contains("PostProcess/Internal/BloomChain.cpp"));
  EXPECT_TRUE(cmake_source.contains("PostProcess/Passes/TonemapPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("PostProcess/Passes/BloomPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("PostProcess/Passes/ExposurePass.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("PostProcess/Types/PostProcessFrameBindings.h"));
}

} // namespace

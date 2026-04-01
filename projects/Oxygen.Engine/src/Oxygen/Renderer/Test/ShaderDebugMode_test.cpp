//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Renderer/Types/ShaderDebugMode.h>
#include <Oxygen/Testing/GTest.h>

namespace {

auto DeviceDepthFromLinearViewDepth(
  const float linear_depth, const glm::mat4& projection_matrix) -> float
{
  const auto clip
    = projection_matrix * glm::vec4(0.0F, 0.0F, -linear_depth, 1.0F);
  return clip.z / std::max(clip.w, 1.0e-6F);
}

auto ReconstructLinearEyeDepthForTest(
  const float device_depth, const glm::mat4& projection_matrix) -> float
{
  const float projection_zz = projection_matrix[2][2];
  const float projection_zw = projection_matrix[2][3];
  const float projection_wz = projection_matrix[3][2];
  const float epsilon = 1.0e-6F;

  if (std::abs(projection_zw) > 0.5F) {
    return std::max(
      projection_wz / std::max(device_depth + projection_zz, epsilon), 0.0F);
  }
  if (std::abs(projection_zz) <= epsilon) {
    return 0.0F;
  }

  return std::max((projection_wz - device_depth) / projection_zz, 0.0F);
}

using oxygen::engine::GetShaderDebugDefineName;
using oxygen::engine::IsLightCullingDebugMode;
using oxygen::engine::IsNonIblDebugMode;
using oxygen::engine::ShaderDebugMode;
using oxygen::engine::to_string;
using oxygen::engine::UsesForwardMeshDebugVariant;

NOLINT_TEST(ShaderDebugModeTest, DepthPrepassDebugModesRemainAppendOnly)
{
  EXPECT_EQ(static_cast<int>(ShaderDebugMode::kVirtualShadowMask), 21);
  EXPECT_EQ(static_cast<int>(ShaderDebugMode::kSceneDepthRaw), 22);
  EXPECT_EQ(static_cast<int>(ShaderDebugMode::kSceneDepthLinear), 23);
  EXPECT_EQ(static_cast<int>(ShaderDebugMode::kSceneDepthMismatch), 24);
  EXPECT_EQ(static_cast<int>(ShaderDebugMode::kMaskedAlphaCoverage), 25);
}

NOLINT_TEST(ShaderDebugModeTest, DepthPrepassDebugModesExposeExpectedTraits)
{
  EXPECT_EQ(to_string(ShaderDebugMode::kSceneDepthRaw),
    std::string_view { "SceneDepthRaw" });
  EXPECT_EQ(GetShaderDebugDefineName(ShaderDebugMode::kSceneDepthRaw),
    std::string_view { "DEBUG_SCENE_DEPTH_RAW" });
  EXPECT_EQ(GetShaderDebugDefineName(ShaderDebugMode::kSceneDepthLinear),
    std::string_view { "DEBUG_SCENE_DEPTH_LINEAR" });
  EXPECT_EQ(GetShaderDebugDefineName(ShaderDebugMode::kSceneDepthMismatch),
    std::string_view { "DEBUG_SCENE_DEPTH_MISMATCH" });
  EXPECT_EQ(GetShaderDebugDefineName(ShaderDebugMode::kMaskedAlphaCoverage),
    std::string_view { "DEBUG_MASKED_ALPHA_COVERAGE" });

  EXPECT_TRUE(IsNonIblDebugMode(ShaderDebugMode::kSceneDepthRaw));
  EXPECT_TRUE(IsNonIblDebugMode(ShaderDebugMode::kSceneDepthLinear));
  EXPECT_TRUE(IsNonIblDebugMode(ShaderDebugMode::kSceneDepthMismatch));
  EXPECT_TRUE(IsNonIblDebugMode(ShaderDebugMode::kMaskedAlphaCoverage));

  EXPECT_FALSE(IsLightCullingDebugMode(ShaderDebugMode::kSceneDepthRaw));
  EXPECT_FALSE(IsLightCullingDebugMode(ShaderDebugMode::kSceneDepthLinear));
  EXPECT_FALSE(IsLightCullingDebugMode(ShaderDebugMode::kSceneDepthMismatch));
  EXPECT_FALSE(IsLightCullingDebugMode(ShaderDebugMode::kMaskedAlphaCoverage));

  EXPECT_FALSE(UsesForwardMeshDebugVariant(ShaderDebugMode::kSceneDepthRaw));
  EXPECT_FALSE(UsesForwardMeshDebugVariant(ShaderDebugMode::kSceneDepthLinear));
  EXPECT_FALSE(
    UsesForwardMeshDebugVariant(ShaderDebugMode::kSceneDepthMismatch));
  EXPECT_FALSE(
    UsesForwardMeshDebugVariant(ShaderDebugMode::kMaskedAlphaCoverage));
}

NOLINT_TEST(
  ShaderDebugModeTest, PerspectiveSceneDepthLinearizationMatchesViewDepth)
{
  const auto projection = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
    std::numbers::pi_v<float> * 0.5F, 1.0F, 0.1F, 100.0F);

  for (const float linear_depth : { 0.1F, 0.5F, 5.0F, 37.5F, 100.0F }) {
    const float device_depth
      = DeviceDepthFromLinearViewDepth(linear_depth, projection);
    EXPECT_NEAR(ReconstructLinearEyeDepthForTest(device_depth, projection),
      linear_depth, 1.0e-4F)
      << "linear_depth=" << linear_depth << " device_depth=" << device_depth;
  }
}

NOLINT_TEST(
  ShaderDebugModeTest, OrthographicSceneDepthLinearizationMatchesViewDepth)
{
  const auto projection = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
    -4.0F, 4.0F, -3.0F, 3.0F, 0.1F, 100.0F);

  for (const float linear_depth : { 0.1F, 1.0F, 12.0F, 64.0F, 100.0F }) {
    const float device_depth
      = DeviceDepthFromLinearViewDepth(linear_depth, projection);
    EXPECT_NEAR(ReconstructLinearEyeDepthForTest(device_depth, projection),
      linear_depth, 1.0e-4F)
      << "linear_depth=" << linear_depth << " device_depth=" << device_depth;
  }
}

} // namespace

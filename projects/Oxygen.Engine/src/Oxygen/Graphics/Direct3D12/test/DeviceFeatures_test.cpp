//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <d3d12.h>
#include <d3dcommon.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/GraphicsCapabilities.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceFeatures.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics::d3d12 {
namespace {

  using ::testing::ElementsAre;
  using ::testing::IsEmpty;

  //! A device that meets the default requirements exactly.
  constexpr auto kQualifying = DeviceFeatures {
    .feature_level = D3D_FEATURE_LEVEL_12_0,
    .shader_model = D3D_SHADER_MODEL_6_8,
    .resource_binding_tier = D3D12_RESOURCE_BINDING_TIER_3,
    .wave_ops = true,
    .memory_architecture = MemoryArchitecture::kDedicated,
  };

  //! A device at every minimum meets the requirements.
  NOLINT_TEST(DeviceFeaturesTest, MinimumDeviceQualifies)
  {
    EXPECT_THAT(MissingRequirements(kQualifying, {}), IsEmpty());
  }

  //! Each requirement below its minimum is reported, in declaration order.
  NOLINT_TEST(DeviceFeaturesTest, ShortfallsFollowDeclarationOrder)
  {
    auto features = kQualifying;
    features.shader_model = D3D_SHADER_MODEL_6_6;
    features.resource_binding_tier = D3D12_RESOURCE_BINDING_TIER_2;
    features.wave_ops = false;

    EXPECT_THAT(MissingRequirements(features, {}),
      ElementsAre(DeviceRequirement::kShaderModel,
        DeviceRequirement::kResourceBindingTier, DeviceRequirement::kWaveOps));
  }

  //! A feature level below the minimum is a shortfall.
  NOLINT_TEST(DeviceFeaturesTest, LowFeatureLevelIsReported)
  {
    auto features = kQualifying;
    features.feature_level = D3D_FEATURE_LEVEL_11_1;

    EXPECT_THAT(MissingRequirements(features, {}),
      ElementsAre(DeviceRequirement::kFeatureLevel));
  }

  //! Wave operations count only when required.
  NOLINT_TEST(DeviceFeaturesTest, WaveOpsCountOnlyWhenRequired)
  {
    auto features = kQualifying;
    features.wave_ops = false;

    EXPECT_THAT(
      MissingRequirements(features, { .wave_ops = false }), IsEmpty());
  }

  //! A shortfall names the requirement and what the device has.
  NOLINT_TEST(DeviceFeaturesTest, ShortfallNamesRequiredAndActualValues)
  {
    auto features = kQualifying;
    features.feature_level = D3D_FEATURE_LEVEL_11_1;
    features.shader_model = D3D_SHADER_MODEL_6_6;
    features.resource_binding_tier = D3D12_RESOURCE_BINDING_TIER_2;

    EXPECT_EQ(DescribeShortfall(DeviceRequirement::kFeatureLevel, features, {}),
      "feature level 12_0 (device has 11_1)");
    EXPECT_EQ(DescribeShortfall(DeviceRequirement::kShaderModel, features, {}),
      "shader model 6.8 (device has 6.6)");
    EXPECT_EQ(
      DescribeShortfall(DeviceRequirement::kResourceBindingTier, features, {}),
      "resource binding tier 3 (device has 2)");
    EXPECT_EQ(DescribeShortfall(DeviceRequirement::kWaveOps, features, {}),
      "wave operations");
  }

  //! The summary lists the required features and only the optional
  //! features the device has.
  NOLINT_TEST(DeviceFeaturesTest, SummaryListsSupportedFeatures)
  {
    const auto full = DeviceFeatures {
      .feature_level = D3D_FEATURE_LEVEL_12_2,
      .shader_model = D3D_SHADER_MODEL_6_9,
      .resource_binding_tier = D3D12_RESOURCE_BINDING_TIER_3,
      .wave_ops = true,
      .memory_architecture = MemoryArchitecture::kDedicated,
      .raytracing_tier = D3D12_RAYTRACING_TIER_1_1,
      .variable_shading_rate_tier = D3D12_VARIABLE_SHADING_RATE_TIER_2,
      .mesh_shader_tier = D3D12_MESH_SHADER_TIER_1,
      .sampler_feedback_tier = D3D12_SAMPLER_FEEDBACK_TIER_1_0,
    };
    auto minimal = kQualifying;
    minimal.wave_ops = false;
    minimal.memory_architecture = MemoryArchitecture::kUnified;

    EXPECT_EQ(nostd::to_string(full),
      "FL 12_2, SM 6.9, tier 3, wave ops, dedicated, RT 1.1, VRS 2, mesh "
      "1, sampler 1.0");
    EXPECT_EQ(nostd::to_string(minimal), "FL 12_0, SM 6.8, tier 3, unified");
  }

} // namespace
} // namespace oxygen::graphics::d3d12

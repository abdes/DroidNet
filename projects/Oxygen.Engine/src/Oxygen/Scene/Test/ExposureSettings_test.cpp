//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <cmath>
#include <limits>

#include <Oxygen/Scene/Camera/CameraExposure.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace {

using oxygen::engine::ExposureMode;
using oxygen::scene::ExposureSettings;
using oxygen::scene::ExposureSettingsError;
using oxygen::scene::ResolveExposureSettings;

NOLINT_TEST(ExposureSettingsTest, FixedGainsPreserveIndependentBinaryReferences)
{
  auto settings = ExposureSettings {};
  settings.mode = ExposureMode::kManual;
  settings.key = 12.5F;
  constexpr auto gains = std::array { 0x1p-14F, 0x1p-15F, 0x1p-16F };
  for (std::size_t i = 0; i < gains.size(); ++i) {
    settings.manual_ev = 14.0F + static_cast<float>(i);
    const auto resolved = ResolveExposureSettings(settings);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->fixed_scale, gains[i]);
  }
}

NOLINT_TEST(ExposureSettingsTest, CoupledBiasAvoidsIntermediateOverflow)
{
  auto settings = ExposureSettings {};
  settings.mode = ExposureMode::kManual;
  settings.key = 25.0F;
  settings.compensation_ev = 160.0F;
  settings.manual_ev = 160.0F;
  const auto resolved = ResolveExposureSettings(settings);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->fixed_scale, 2.0F);
  EXPECT_EQ(
    oxygen::engine::ExposureScaleFromEv100(160.0F, 160.0F, 25.0F), 2.0F);
}

NOLINT_TEST(
  ExposureSettingsTest, CameraMathRejectsInvalidAndAvoidsProductOverflow)
{
  auto camera = oxygen::scene::CameraExposure {};
  EXPECT_NEAR(camera.GetEv(), 13.884647521936682F, 1.0e-6F);
  camera.aperture_f = 0.0F;
  EXPECT_TRUE(std::isnan(camera.GetEv()));
  camera.aperture_f = 0x1p64F;
  camera.shutter_rate = 0x1p-64F;
  camera.iso = 100.0F;
  EXPECT_EQ(camera.GetEv(), 64.0F);
}

NOLINT_TEST(ExposureSettingsTest, GainBoundaryIsValidatedRatherThanClamped)
{
  auto settings = ExposureSettings {};
  settings.mode = ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 32.0F;
  const auto lower = ResolveExposureSettings(settings);
  ASSERT_TRUE(lower.has_value());
  EXPECT_EQ(lower->fixed_scale, 0x1p-32F);
  settings.manual_ev = std::nextafter(32.0F, 33.0F);
  const auto outside = ResolveExposureSettings(settings);
  ASSERT_FALSE(outside.has_value());
  EXPECT_EQ(outside.error(), ExposureSettingsError::kUnsupportedGain);
  settings.manual_ev = -32.0F;
  const auto upper = ResolveExposureSettings(settings);
  ASSERT_TRUE(upper.has_value());
  EXPECT_EQ(upper->fixed_scale, 0x1p32F);
}

NOLINT_TEST(
  ExposureSettingsTest, SubnormalAuthoringDoesNotRequireSubnormalGpuMath)
{
  auto settings = ExposureSettings {};
  settings.mode = ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  settings.key = std::numeric_limits<float>::denorm_min();
  settings.compensation_ev = 149.0F;
  const auto normalized = ResolveExposureSettings(settings);
  ASSERT_TRUE(normalized.has_value());
  EXPECT_NEAR(normalized->fixed_scale, 0.08F, 1.0e-7F);

  settings.key = 12.5F;
  settings.compensation_ev = 0.0F;
  for (const float ev : { 126.0F, 149.0F }) {
    settings.manual_ev = ev;
    const auto outside = ResolveExposureSettings(settings);
    ASSERT_FALSE(outside.has_value());
    EXPECT_EQ(outside.error(), ExposureSettingsError::kUnsupportedGain);
  }
  settings = {};
  settings.min_log_luminance = 0.0F;
  settings.log_luminance_range = std::numeric_limits<float>::denorm_min();
  const auto reciprocal_overflow = ResolveExposureSettings(settings);
  ASSERT_FALSE(reciprocal_overflow.has_value());
  EXPECT_EQ(reciprocal_overflow.error(),
    ExposureSettingsError::kInvalidHistogramWindow);
}

NOLINT_TEST(ExposureSettingsTest, PhysicalCameraRequiresExplicitValidCameraEv)
{
  auto settings = ExposureSettings {};
  settings.mode = ExposureMode::kManualCamera;
  settings.key = 12.5F;
  const auto missing = ResolveExposureSettings(settings);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), ExposureSettingsError::kMissingCameraEv);
  const auto physical = ResolveExposureSettings(settings, 13.884647521936682F);
  ASSERT_TRUE(physical.has_value());
  EXPECT_NEAR(physical->fixed_scale, 1.0F / 15125.0F, 1.0e-10F);
  EXPECT_FALSE(
    ResolveExposureSettings(settings, std::numeric_limits<float>::quiet_NaN())
      .has_value());
}

NOLINT_TEST(ExposureSettingsTest, ZeroTargetAndPausedRatesAreLegal)
{
  auto settings = ExposureSettings {};
  settings.target_luminance = 0.0F;
  settings.speed_up = settings.speed_down = 0.0F;
  const auto resolved = ResolveExposureSettings(settings);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->authored.target_luminance, 0.0F);
  EXPECT_NEAR(std::exp2(resolved->initial_log_gain), 0.8F, 1.0e-6F);
  EXPECT_TRUE(std::isfinite(resolved->initial_log_gain));
}

NOLINT_TEST(
  ExposureSettingsTest, CurveInteriorExtremaCannotEscapeGainValidation)
{
  auto settings = ExposureSettings {};
  settings.key = 12.5F;
  settings.compensation_curve
    = { { -6.0F, 0.0F }, { 5.0F, 40.0F }, { 16.0F, 0.0F } };
  const auto resolved = ResolveExposureSettings(settings);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error(), ExposureSettingsError::kUnsupportedGain);

  settings.compensation_curve[1].compensation_ev = 1.0F;
  settings.min_ev = settings.max_ev = 5.0F;
  EXPECT_TRUE(ResolveExposureSettings(settings).has_value());
}

NOLINT_TEST(
  ExposureSettingsTest, InvalidCurveCannotPartiallyNormalizeAuthoredKeys)
{
  auto settings = ExposureSettings {};
  settings.compensation_curve = { { 1.0F, 0.0F }, { 1.0F, 2.0F } };
  const auto original = settings;
  const auto resolved = ResolveExposureSettings(settings);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error(), ExposureSettingsError::kInvalidCurve);
  EXPECT_EQ(settings, original);
  settings.compensation_curve.resize(65);
  EXPECT_FALSE(ResolveExposureSettings(settings).has_value());
}

NOLINT_TEST(ExposureSettingsTest, CoupledRangesAndNonfiniteFieldsAreRejected)
{
  auto settings = ExposureSettings {};
  settings.low_percentile = settings.high_percentile;
  EXPECT_EQ(ResolveExposureSettings(settings).error(),
    ExposureSettingsError::kInvalidPercentiles);
  settings = {};
  settings.min_ev = settings.max_ev + 1.0F;
  EXPECT_EQ(ResolveExposureSettings(settings).error(),
    ExposureSettingsError::kInvalidEvRange);
  settings = {};
  settings.transition_distance = 0.0F;
  EXPECT_EQ(ResolveExposureSettings(settings).error(),
    ExposureSettingsError::kInvalidTransitionDistance);
  settings = {};
  settings.key = std::numeric_limits<float>::infinity();
  EXPECT_EQ(ResolveExposureSettings(settings).error(),
    ExposureSettingsError::kNonFinite);
}

NOLINT_TEST(ExposureSettingsTest, DisabledModePublishesUnitGain)
{
  auto settings = ExposureSettings {};
  settings.enabled = false;
  const auto resolved = ResolveExposureSettings(settings);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->fixed_scale, 1.0F);
}

NOLINT_TEST(ExposureSettingsTest, AutoCancellationPreservesSmallKeyBias)
{
  auto settings = ExposureSettings {};
  settings.key = 1.0e20F;
  settings.compensation_ev = 1.0e20F;
  settings.compensation_curve = { { 0.0F, -1.0e20F } };
  const auto invalid = ResolveExposureSettings(settings);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error(), ExposureSettingsError::kUnsupportedGain);
  settings.key = 25.0F;
  const auto valid = ResolveExposureSettings(settings);
  ASSERT_TRUE(valid.has_value());
  EXPECT_NEAR(valid->initial_log_gain, 1.0F, 2.0e-6F);
  EXPECT_NEAR(valid->dark_log_gain, 7.0F, 2.0e-6F);
}

NOLINT_TEST(
  ExposureSettingsTest, LockedAutoUploadsBoundedTargetsAfterCancellation)
{
  for (const float ev : { -160.0F, 160.0F }) {
    auto settings = ExposureSettings {};
    settings.key = 12.5F;
    settings.min_ev = settings.max_ev = ev;
    settings.compensation_ev = ev;
    const auto valid = ResolveExposureSettings(settings);
    ASSERT_TRUE(valid.has_value());
    ASSERT_EQ(valid->auto_log_targets.size(), 1U);
    EXPECT_NEAR(valid->auto_log_targets[0].log_gain, 0.0F, 2.0e-6F);
    EXPECT_TRUE(std::isfinite(valid->initial_log_gain));
  }
}

NOLINT_TEST(ExposureSettingsTest, TargetKnotsCoverRetainedMeterOutsideNewWindow)
{
  auto settings = ExposureSettings {};
  settings.min_log_luminance = 0.0F;
  settings.log_luminance_range = 1.0F;
  const auto resolved = ResolveExposureSettings(settings);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_LT(resolved->auto_log_targets.front().metered_ev, -21.0F);
  EXPECT_GT(resolved->auto_log_targets.back().metered_ev, 34.0F);
  settings.compensation_curve = { { 25.0F, 60.0F } };
  EXPECT_EQ(ResolveExposureSettings(settings).error(), ExposureSettingsError::kUnsupportedGain);
}

} // namespace

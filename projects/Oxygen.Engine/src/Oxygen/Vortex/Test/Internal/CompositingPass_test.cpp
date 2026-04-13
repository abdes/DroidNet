//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <limits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Testing/ScopedLogCapture.h>
#include <Oxygen/Vortex/Internal/CompositingAlphaSanitizer.h>

namespace {

using oxygen::testing::ScopedLogCapture;
using oxygen::vortex::internal::detail::AlphaWarningLimiter;
using oxygen::vortex::internal::detail::SanitizeCompositingAlpha;

TEST(CompositingPassAlphaSanitizerTest,
  InvalidAlphaWarningIsOnlyEmittedOncePerDebugName)
{
  AlphaWarningLimiter limiter;
  ScopedLogCapture capture(
    "CompositingInvalidAlpha", loguru::Verbosity_WARNING);
  const auto invalid_alpha = std::numeric_limits<float>::quiet_NaN();

  const auto first
    = SanitizeCompositingAlpha("MainView", invalid_alpha, limiter);
  if (first.log_invalid_alpha) {
    LOG_F(WARNING, "invalid alpha={}, resetting to default {}", invalid_alpha,
      first.alpha);
  }

  const auto second
    = SanitizeCompositingAlpha("MainView", invalid_alpha, limiter);
  if (second.log_invalid_alpha) {
    LOG_F(WARNING, "invalid alpha={}, resetting to default {}", invalid_alpha,
      second.alpha);
  }

  EXPECT_TRUE(first.log_invalid_alpha);
  EXPECT_FALSE(second.log_invalid_alpha);
  EXPECT_EQ(capture.Count("invalid alpha="), 1);
}

TEST(CompositingPassAlphaSanitizerTest,
  ClampedAlphaWarningIsOnlyEmittedOncePerDebugName)
{
  AlphaWarningLimiter limiter;
  ScopedLogCapture capture(
    "CompositingClampedAlpha", loguru::Verbosity_WARNING);

  const auto first = SanitizeCompositingAlpha("HudOverlay", 2.5F, limiter);
  if (first.log_clamped_alpha) {
    LOG_F(WARNING, "clamping alpha={} to {}", 2.5F, first.alpha);
  }

  const auto second = SanitizeCompositingAlpha("HudOverlay", 2.5F, limiter);
  if (second.log_clamped_alpha) {
    LOG_F(WARNING, "clamping alpha={} to {}", 2.5F, second.alpha);
  }

  EXPECT_TRUE(first.log_clamped_alpha);
  EXPECT_FALSE(second.log_clamped_alpha);
  EXPECT_EQ(capture.Count("clamping alpha="), 1);
}

TEST(CompositingPassAlphaSanitizerTest,
  WarningLimiterTracksDifferentDebugNamesIndependently)
{
  AlphaWarningLimiter limiter;

  const auto invalid_alpha = std::numeric_limits<float>::quiet_NaN();
  const auto a = SanitizeCompositingAlpha("MainView", invalid_alpha, limiter);
  const auto b = SanitizeCompositingAlpha("MiniMap", invalid_alpha, limiter);

  EXPECT_TRUE(a.log_invalid_alpha);
  EXPECT_TRUE(b.log_invalid_alpha);
}

} // namespace

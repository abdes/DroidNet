//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Internal/Jobs/TextureImportPolicy.h>

using oxygen::content::import::ImportOptions;
using oxygen::content::import::TexturePipeline;
using oxygen::content::import::detail::FailurePolicyForTextureTuning;

namespace {

//! Verify strict policy when placeholder-on-failure is disabled.
NOLINT_TEST(TextureImportJobPolicyTest,
  FailurePolicyDefaultsToStrictWhenPlaceholderDisabled)
{
  // Arrange
  ImportOptions::TextureTuning tuning {};
  tuning.placeholder_on_failure = false;

  // Act
  const auto policy = FailurePolicyForTextureTuning(tuning);

  // Assert
  EXPECT_EQ(policy, TexturePipeline::FailurePolicy::kStrict);
}

//! Verify placeholder policy when placeholder-on-failure is enabled.
NOLINT_TEST(TextureImportJobPolicyTest,
  FailurePolicyUsesPlaceholderWhenPlaceholderEnabled)
{
  // Arrange
  ImportOptions::TextureTuning tuning {};
  tuning.placeholder_on_failure = true;

  // Act
  const auto policy = FailurePolicyForTextureTuning(tuning);

  // Assert
  EXPECT_EQ(policy, TexturePipeline::FailurePolicy::kPlaceholder);
}

} // namespace

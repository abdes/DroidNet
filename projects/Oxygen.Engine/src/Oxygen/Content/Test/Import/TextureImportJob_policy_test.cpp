//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Jobs/TextureImportPolicy.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::import::ImportOptions;
using oxygen::content::import::TexturePipeline;
using oxygen::content::import::detail::FailurePolicyForTextureTuning;

namespace {

//! Verify strict policy when placeholder-on-failure is disabled.
NOLINT_TEST(TextureImportJobPolicyTest,
  FailurePolicy_DefaultsToStrict_WhenPlaceholderDisabled)
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
  FailurePolicy_UsesPlaceholder_WhenPlaceholderEnabled)
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

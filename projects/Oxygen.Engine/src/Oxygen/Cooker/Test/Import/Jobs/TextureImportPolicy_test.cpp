//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/TextureImportPolicy.h

#include <Oxygen/Cooker/Import/Internal/Jobs/TextureImportPolicy.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::import::ImportOptions;
using oxygen::content::import::TexturePipeline;
using oxygen::content::import::detail::FailurePolicyForTextureTuning;

namespace {

NOLINT_TEST(TextureImportJobPolicyTest,
  FailurePolicyDefaultsToStrictWhenPlaceholderDisabled)
{
  ImportOptions::TextureTuning tuning {};
  tuning.placeholder_on_failure = false;

  const auto policy = FailurePolicyForTextureTuning(tuning);

  EXPECT_EQ(policy, TexturePipeline::FailurePolicy::kStrict);
}

NOLINT_TEST(TextureImportJobPolicyTest,
  FailurePolicyUsesPlaceholderWhenPlaceholderEnabled)
{
  ImportOptions::TextureTuning tuning {};
  tuning.placeholder_on_failure = true;

  const auto policy = FailurePolicyForTextureTuning(tuning);

  EXPECT_EQ(policy, TexturePipeline::FailurePolicy::kPlaceholder);
}

} // namespace

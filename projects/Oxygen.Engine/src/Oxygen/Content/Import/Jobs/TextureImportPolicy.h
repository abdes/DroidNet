//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/Pipelines/TexturePipeline.h>

namespace oxygen::content::import::detail {

//! Selects the pipeline failure policy from texture tuning settings.
[[nodiscard]] inline auto FailurePolicyForTextureTuning(
  const ImportOptions::TextureTuning& tuning) -> TexturePipeline::FailurePolicy
{
  return tuning.placeholder_on_failure
    ? TexturePipeline::FailurePolicy::kPlaceholder
    : TexturePipeline::FailurePolicy::kStrict;
}

} // namespace oxygen::content::import::detail

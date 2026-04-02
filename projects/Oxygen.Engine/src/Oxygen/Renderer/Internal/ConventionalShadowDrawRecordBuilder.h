//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Types/ConventionalShadowDrawRecord.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::internal {

[[nodiscard]] OXGN_RNDR_API auto IsConventionalShadowRasterPartition(
  PassMask pass_mask) noexcept -> bool;

OXGN_RNDR_API auto BuildConventionalShadowDrawRecords(
  const PreparedSceneFrame& prepared_frame,
  std::vector<renderer::ConventionalShadowDrawRecord>& out_records) -> void;

} // namespace oxygen::engine::internal

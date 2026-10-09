//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::vortex {

//! Revisions of renderer-resident content that can change what a view shows
//! without any scene edit: streamed textures, uploaded geometry, refreshed
//! environment probes and exposure metering masks.
/*!
 A host that renders views on demand compares successive values: any
 difference means a view rendered before may now be stale. Members are
 independent revisions, not counts, and are only compared for equality.
*/
struct ResidentContentRevision {
  std::uint64_t textures { 0U };
  std::uint64_t geometry { 0U };
  std::uint64_t environment_probes { 0U };
  std::uint64_t exposure_masks { 0U };

  auto operator==(const ResidentContentRevision&) const -> bool = default;
};

} // namespace oxygen::vortex

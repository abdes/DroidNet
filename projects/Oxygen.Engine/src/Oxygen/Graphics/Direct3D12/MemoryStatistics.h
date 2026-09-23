//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Types/ByteUnits.h>

namespace oxygen::graphics::d3d12 {

//! Allocator totals for one DXGI memory segment, sampled on demand.
struct MemorySegmentStatistics {
  std::uint32_t allocation_count { 0U };
  std::uint32_t block_count { 0U };
  //! Occupied allocations, including resources retained for deferred release.
  SizeBytes allocation_bytes { 0U };
  //! Allocator-owned heaps and committed blocks, including unused capacity.
  SizeBytes block_bytes { 0U };
  //! Process usage estimate; includes objects outside the allocator.
  SizeBytes estimated_usage_bytes { 0U };
  //! OS budget estimate, not physical adapter capacity or an engine
  //! reservation.
  SizeBytes estimated_budget_bytes { 0U };
};

struct MemoryStatistics {
  MemorySegmentStatistics local;
  MemorySegmentStatistics non_local;
};

} // namespace oxygen::graphics::d3d12

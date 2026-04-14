//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>

#include <Oxygen/Profiling/ProfileScope.h>

namespace oxygen::profiling::internal {

struct CpuScopeState {
  alignas(std::max_align_t) std::byte tracy_zone_state[16] {};
  uint8_t flags { 0 };
};

OXGN_PROF_NDAPI auto BeginCpuScope(const CpuProfileScopeDesc& desc,
  std::source_location callsite) -> CpuScopeState;
OXGN_PROF_API auto EndCpuScope(const CpuScopeState& state) -> void;

} // namespace oxygen::profiling::internal

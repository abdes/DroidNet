//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Profiling/ProfileScope.h>

namespace oxygen::profiling {

class CpuProfileScope {
public:
  OXGN_PROF_API explicit CpuProfileScope(const CpuProfileScopeDesc& desc,
    std::source_location callsite = std::source_location::current());
  OXGN_PROF_API explicit CpuProfileScope(std::string_view label,
    ProfileCategory category = ProfileCategory::kGeneral,
    ScopeVariables variables = {}, ProfileColor color = {},
    std::source_location callsite = std::source_location::current());
  OXGN_PROF_API ~CpuProfileScope();

  OXYGEN_MAKE_NON_COPYABLE(CpuProfileScope)
  OXYGEN_MAKE_NON_MOVABLE(CpuProfileScope)

private:
  alignas(std::max_align_t) std::byte tracy_zone_state_[16] {};
  uint8_t flags_ { 0 };
};

} // namespace oxygen::profiling

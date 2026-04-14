//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Profiling/CpuProfileScope.h>

#include <cstring>

#include <Oxygen/Profiling/Internal/CpuProfileCoordinator.h>

namespace oxygen::profiling {

CpuProfileScope::CpuProfileScope(
  const CpuProfileScopeDesc& desc, const std::source_location callsite)
{
  const auto state = internal::BeginCpuScope(desc, callsite);
  std::memcpy(
    tracy_zone_state_, state.tracy_zone_state, sizeof(tracy_zone_state_));
  flags_ = state.flags;
}

CpuProfileScope::CpuProfileScope(const std::string_view label,
  const ProfileCategory category, ScopeVariables variables,
  const ProfileColor color, const std::source_location callsite)
  : CpuProfileScope(
      CpuProfileScopeDesc {
        .label = std::string(label),
        .variables = std::move(variables),
        .category = category,
        .color = color,
      },
      callsite)
{
}

CpuProfileScope::~CpuProfileScope()
{
  internal::CpuScopeState state {};
  std::memcpy(
    state.tracy_zone_state, tracy_zone_state_, sizeof(tracy_zone_state_));
  state.flags = flags_;
  internal::EndCpuScope(state);
}

} // namespace oxygen::profiling

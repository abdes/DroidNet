//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Profiling/Internal/CpuProfileCoordinator.h>

#include <cstring>

#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Tracy/Cpu.h>

#if defined(USE_PIX) && __has_include(<pix3.h>)
#  include <pix3.h>
#endif

namespace oxygen::profiling::internal {
namespace {
  constexpr uint8_t kCpuScopeFlagPixActive = 1U << 0U;
  constexpr uint8_t kCpuScopeFlagTracyActive = 1U << 1U;
} // namespace

auto BeginCpuScope(const CpuProfileScopeDesc& desc,
  const std::source_location callsite) -> CpuScopeState
{
  CpuScopeState state {};
  const auto formatted_name = FormatScopeName(desc);

#if defined(OXYGEN_WITH_TRACY)
  const auto effective_color = desc.color.IsSpecified()
    ? desc.color
    : DefaultProfileColor(desc.category);
  if (oxygen::tracy::cpu::BeginZone(std::span { state.tracy_zone_state },
        callsite, formatted_name, effective_color.Rgb24())) {
    state.flags |= kCpuScopeFlagTracyActive;
  }
#endif

#if defined(USE_PIX) && __has_include(<pix3.h>)
  PIXBeginEvent(0U, "%s", formatted_name.c_str());
  state.flags |= kCpuScopeFlagPixActive;
#endif

  return state;
}

auto EndCpuScope(const CpuScopeState& state) -> void
{
#if defined(USE_PIX) && __has_include(<pix3.h>)
  if ((state.flags & kCpuScopeFlagPixActive) != 0U) {
    PIXEndEvent();
  }
#endif

#if defined(OXYGEN_WITH_TRACY)
  if ((state.flags & kCpuScopeFlagTracyActive) != 0U) {
    oxygen::tracy::cpu::EndZone(std::span { state.tracy_zone_state });
  }
#endif
}

} // namespace oxygen::profiling::internal

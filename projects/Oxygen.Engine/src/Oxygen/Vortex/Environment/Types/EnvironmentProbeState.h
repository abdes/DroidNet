//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kEnvironmentProbeStateFlagResourcesValid
  = 1U << 0U;
inline constexpr std::uint32_t kEnvironmentProbeStateFlagUnavailable = 1U << 1U;
inline constexpr std::uint32_t kEnvironmentProbeStateFlagStale = 1U << 2U;

struct EnvironmentProbeState {
  EnvironmentProbeBindings probes {};
  std::uint32_t flags { 0U };
  bool valid { false };
};

} // namespace oxygen::vortex

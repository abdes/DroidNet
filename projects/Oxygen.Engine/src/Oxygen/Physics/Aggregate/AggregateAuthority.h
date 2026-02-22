//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics::aggregate {

//! Motion authority contract for aggregate-owned simulation objects.
enum class AggregateAuthority : uint8_t {
  kSimulation = 0, //!< Physics solver owns motion; scene consumes pull results.
  kCommand = 1, //!< Gameplay commands own intent; backend integrates controls.
};

OXGN_PHYS_NDAPI auto to_string(AggregateAuthority value) noexcept
  -> std::string_view;

} // namespace oxygen::physics::aggregate

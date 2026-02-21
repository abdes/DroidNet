//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics {

enum class PhysicsBackend : uint8_t {
  kNone = 0,
  kJolt,
};

OXGN_PHYS_NDAPI auto to_string(PhysicsBackend value) noexcept
  -> std::string_view;

} // namespace oxygen::physics

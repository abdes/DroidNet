//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
namespace oxygen::vortex {
enum class ShadowUseError : std::uint8_t {
  kClosed,
  kNotReady,
  kWrongBackend,
  kWrongQueue,
  kAllocationFailed
};
}

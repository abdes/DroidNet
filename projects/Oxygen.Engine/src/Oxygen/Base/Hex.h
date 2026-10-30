//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Base/api_export.h>

namespace oxygen::base {

//! Lowercase hexadecimal digits, two per byte, first byte first.
OXGN_BASE_NDAPI auto ToHexString(std::span<const std::byte> bytes)
  -> std::string;

//! Fills `out` from exactly `2 * out.size()` hexadecimal digits of either
//! case; false for any other text, leaving `out` unspecified.
OXGN_BASE_NDAPI auto FromHexString(
  std::string_view text, std::span<std::byte> out) noexcept -> bool;

} // namespace oxygen::base

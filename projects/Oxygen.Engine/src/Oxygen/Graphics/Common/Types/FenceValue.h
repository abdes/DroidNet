//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Strong fence type wrapping the raw uint64_t returned by command queue
//! Signal/GetCompletedValue operations.
using FenceValue = NamedType<uint64_t, struct FenceValueTag,
  // clang-format off
  DefaultInitialized,
  Comparable,
  Printable,
  Hashable>; // clang-format on

//! Convert a FenceValue to a human-readable string.
OXGN_GFX_API auto to_string(FenceValue v) -> std::string;

//! Explicit namespace with concise aliases for fence-related strong types to
//! improve ergonomic at use sites.
namespace fence {
  using Value = FenceValue;

  //! Sentinel invalid fence value.
  inline constexpr FenceValue kInvalidValue {
    (std::numeric_limits<uint64_t>::max)(),
  };
} // namespace fence

} // namespace oxygen::graphics

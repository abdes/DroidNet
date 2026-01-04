//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::engine {

//! Strong type representing a draw index into DrawMetadata.
/*!
 Used as the root-constant `g_DrawIndex` payload to index into the per-draw
 `DrawMetadata` structured buffer.
*/
using DrawIndex = oxygen::NamedType<uint32_t, struct DrawIndexTag,
  // clang-format off
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable>; // clang-format on

//! Convert DrawIndex to a human-readable string representation.
[[nodiscard]] inline auto to_string(const DrawIndex idx) -> std::string
{
  return std::to_string(idx.get());
}

} // namespace oxygen::engine

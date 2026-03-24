//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>

namespace oxygen {

using SizeBytes = NamedType<uint64_t, struct BytesTag,
  // clang-format off
  DefaultInitialized,
  Arithmetic>; // clang-format on

inline auto to_string(SizeBytes const& b)
{
  return std::to_string(b.get()) + " bytes";
}

using OffsetBytes = NamedType<uint64_t, struct OffsetBytesTag,
  // clang-format off
  DefaultInitialized,
  Arithmetic>; // clang-format on

inline auto to_string(OffsetBytes const& b)
{
  return std::to_string(b.get()) + " bytes";
}

using Alignment = NamedType<uint32_t, struct AlignmentTag,
  // clang-format off
  DefaultInitialized,
  Comparable,
  Printable>; // clang-format on

inline auto to_string(Alignment const& a) { return std::to_string(a.get()); }

} // namespace oxygen

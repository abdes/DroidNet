//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/NamedType.h>

namespace oxygen {

using ResourceIndexT = NamedType<uint32_t, struct ResourceIndexTag,
  // clang-format off
  DefaultInitialized,
  ImplicitlyConvertibleTo<uint32_t>::templ,
  Comparable,
  Hashable,
  Printable>; // clang-format on

static_assert(sizeof(ResourceIndexT) == sizeof(uint32_t),
  "ResourceIndex contract violated: ResourceIndexT size must be 4 bytes");

} // namespace oxygen

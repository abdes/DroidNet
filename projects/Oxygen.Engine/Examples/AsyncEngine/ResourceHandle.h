//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <Oxygen/Base/NamedType.h>

namespace oxygen::examples::asyncsim {

//! Strong types for render graph resource integration (exported - not
//! anonymous)
// clang-format off
using ResourceHandle = oxygen::NamedType<uint64_t, struct ResourceHandleTag,
  oxygen::Hashable,
  oxygen::Comparable,
  oxygen::Printable>;

} // namespace oxygen::examples::asyncsim

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Serio/Error.h>

namespace oxygen::serio::detail {

//! Whether an OS error value (`std::system_category()`) means a Serio
//! condition. Each platform directory implements it.
[[nodiscard]] auto IsSystemErrorCondition(int value, Error condition) noexcept
  -> bool;

} // namespace oxygen::serio::detail

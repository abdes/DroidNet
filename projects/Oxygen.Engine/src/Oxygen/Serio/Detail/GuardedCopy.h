//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio::detail {

//! Copies `size` bytes from mapped memory, returning false instead of
//! crashing when a page of `source` fails to page in (`EXCEPTION_IN_PAGE_ERROR`
//! on Windows, `SIGBUS` elsewhere). Any other fault is not handled.
OXGN_SERIO_NDAPI auto GuardedCopy(std::byte* destination,
  const std::byte* source, size_t size) noexcept -> bool;

} // namespace oxygen::serio::detail

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/OxCo/Coroutine.h>

namespace oxygen::co {

//! An awaiter similar to `std::suspend_always`, but with cancellation
//! support.
class SuspendForever {
public:
  [[nodiscard]] auto await_ready() const noexcept { return false; }
  void await_suspend([[maybe_unused]] detail::Handle h) { }
  [[noreturn]] void await_resume() { Unreachable(); }
  auto await_cancel([[maybe_unused]] detail::Handle h) noexcept
  {
    return std::true_type {};
  }
};

} // namespace oxygen::co

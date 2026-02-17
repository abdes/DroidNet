//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>

namespace oxygen {

template <typename Fn>
concept NoExceptCallable = requires(Fn fn) {
  { fn() } noexcept;
};

//! A scope guard that runs a function when it goes out of scope.
template <NoExceptCallable Fn> class ScopeGuard {
public:
  explicit ScopeGuard(Fn fn)
    : fn_(std::move(fn))
  {
  }

  ~ScopeGuard() noexcept { fn_(); }

  OXYGEN_MAKE_NON_COPYABLE(ScopeGuard)
  OXYGEN_MAKE_NON_MOVABLE(ScopeGuard)

private:
  OXYGEN_NO_UNIQUE_ADDRESS Fn fn_;
};

} // namespace oxygen

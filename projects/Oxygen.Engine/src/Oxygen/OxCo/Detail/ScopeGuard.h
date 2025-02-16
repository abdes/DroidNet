//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include "Oxygen/Base/Macros.h"

namespace oxygen::co::detail {

template <typename Fn>
concept NoExceptCallable = requires(Fn fn) {
    { fn() } noexcept;
};

//! A scope guard that runs a function when it goes out of scope.
template <NoExceptCallable Fn>
class ScopeGuard {
public:
    explicit ScopeGuard(Fn fn)
        : fn_(std::move(fn))
    {
    }
    ~ScopeGuard() noexcept { fn_(); }
    OXYGEN_MAKE_NON_COPYABLE(ScopeGuard)
    OXYGEN_MAKE_NON_MOVABLE(ScopeGuard)

private:
    [[no_unique_address]] Fn fn_;
};

} // namespace oxygen::co::detail

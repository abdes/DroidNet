//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include "Oxygen/Base/Unreachable.h"
#include "Oxygen/OxCo/Coroutine.h"

namespace oxygen::co {

//! An awaitable similar to `std::suspend_always`, but with cancellation
//! support.
class SuspendForever {
public:
    // ReSharper disable CppMemberFunctionMayBeStatic
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[nodiscard]] auto await_ready() const noexcept { return false; }
    void await_suspend([[maybe_unused]] detail::Handle h) { }
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[noreturn]] void await_resume() { Unreachable(); }
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    auto await_cancel([[maybe_unused]] detail::Handle h) noexcept
    {
        return std::true_type {};
    }
    // ReSharper restore CppMemberFunctionMayBeStatic
};

} // namespace oxygen::co

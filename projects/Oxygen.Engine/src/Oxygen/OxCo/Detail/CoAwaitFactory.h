//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::co::detail {

//! A utility class to help define constant awaitables such as
//! `kSuspendForever`.
/*!
 Example:
 \code{.cpp}
    class SuspendForever {
    public:
        bool await_ready() const noexcept { return false; }
        ...
    };
    static constexpr detail::CoAwaitFactory<SuspendForever> kSuspendForever;

    ...
    co_await kSuspendForever;
 \endcode
 */
template <class T>
class CoAwaitFactory {
public:
    auto operator co_await() const noexcept { return T {}; }
};

} // namespace oxygen::co::detail

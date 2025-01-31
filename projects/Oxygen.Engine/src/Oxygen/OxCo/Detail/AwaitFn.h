//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Concepts/Cancellation.h"
#include "Oxygen/OxCo/Coroutine.h"

namespace oxygen::co::detail {

//! @{
//! Wrappers around await_*() awaitable functions

//! A sanitized version of `await_suspend()` which always returns a Handle.
template <class Aw, class Promise>
auto AwaitSuspend(Aw&& awaitable, CoroutineHandle<Promise> h) -> Handle
{
    // Note: Aw is unconstrained here, as an awaitable requiring being
    // rvalue-qualified is still passed by lvalue (we're not consuming
    // them until `await_resume()`).
    using RetType = decltype(std::forward<Aw>(awaitable).await_suspend(h));
    if constexpr (std::is_same_v<RetType, void>) {
        std::forward<Aw>(awaitable).await_suspend(h);
        return std::noop_coroutine();
    } else if constexpr (std::is_convertible_v<RetType, Handle>) {
        return std::forward<Aw>(awaitable).await_suspend(h);
    } else {
        if (std::forward<Aw>(awaitable).await_suspend(h)) {
            return std::noop_coroutine();
        }
        return h;
    }
}

//! A sanitized version of `await_early_cancel()` which defaults to `true`.
template <class Aw>
auto AwaitEarlyCancel(Aw& awaitable) noexcept
{
    if constexpr (CustomizesEarlyCancel<Aw>) {
        return awaitable.await_early_cancel();
    } else {
        return std::true_type {};
    }
}

//! A sanitized version of `await_cancel()` which defaults to `false`.
template <class Aw>
auto AwaitCancel(Aw& awaitable, Handle h) noexcept
{
    if constexpr (Cancellable<Aw>) {
        return awaitable.await_cancel(h);
    } else {
        return std::false_type {};
    }
}

//! A sanitized version of `await_must_resume()` which defaults to `true` if the
//! awaitable is not cancellable.
template <class Aw>
auto AwaitMustResume(const Aw& awaitable) noexcept
{
    if constexpr (CustomizesMustResume<Aw>) {
        return awaitable.await_must_resume();
    } else if constexpr (CancelAlwaysSucceeds<Aw>) {
        return std::false_type {};
    } else {
        static_assert(!Cancellable<Aw>);
        return std::true_type {};
    }
}

//! @

} // namespace oxygen::co::detail

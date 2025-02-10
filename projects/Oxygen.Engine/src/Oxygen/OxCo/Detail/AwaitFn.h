//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Coroutine.h"

namespace oxygen::co::detail {

//! @{
//! Wrappers around await_*() awaiter functions

//! A sanitized version of `await_suspend()` which always returns a Handle.
template <class Awaiter, class Promise>
auto AwaitSuspend(Awaiter&& awaiter, CoroutineHandle<Promise> h) -> Handle
{
    // Note: Awaiter is unconstrained here, as an awaiter requiring being
    // rvalue-qualified is still passed by lvalue (we're not consuming
    // them until `await_resume()`).
    using RetType = decltype(std::forward<Awaiter>(awaiter).await_suspend(h));
    if constexpr (std::is_same_v<RetType, void>) {
        std::forward<Awaiter>(awaiter).await_suspend(h);
        return std::noop_coroutine();
    } else if constexpr (std::is_convertible_v<RetType, Handle>) {
        return std::forward<Awaiter>(awaiter).await_suspend(h);
    } else {
        if (std::forward<Awaiter>(awaiter).await_suspend(h)) {
            return std::noop_coroutine();
        }
        return h;
    }
}

//! A sanitized version of `await_early_cancel()` which defaults to `true`.
template <class Awaiter>
auto AwaitEarlyCancel(Awaiter& awaiter) noexcept
{
    if constexpr (CustomizesEarlyCancel<Awaiter>) {
        return awaiter.await_early_cancel();
    } else {
        return std::true_type {};
    }
}

//! A sanitized version of `await_cancel()` which defaults to `false`.
template <class Awaiter>
auto AwaitCancel(Awaiter& awaiter, Handle h) noexcept
{
    if constexpr (Cancellable<Awaiter>) {
        return awaiter.await_cancel(h);
    } else {
        return std::false_type {};
    }
}

//! A sanitized version of `await_must_resume()` which defaults to `true` if the
//! awaiter is not cancellable.
template <class Awaiter>
auto AwaitMustResume(const Awaiter& awaiter) noexcept
{
    if constexpr (CustomizesMustResume<Awaiter>) {
        return awaiter.await_must_resume();
    } else if constexpr (CancelAlwaysSucceeds<Awaiter>) {
        return std::false_type {};
    } else {
        static_assert(!Cancellable<Awaiter>);
        return std::true_type {};
    }
}

template <class Awaiter>
void AwaitSetExecutor(Awaiter& awaiter, Executor* ex) noexcept
{
    if constexpr (NeedsExecutor<Awaiter>) {
        awaiter.await_set_executor(ex);
    }
}

//! @

} // namespace oxygen::co::detail

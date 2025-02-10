//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/AwaitFn.h"
#include "Oxygen/OxCo/Detail/AwaitableStateChecker.h"
#include "Oxygen/OxCo/Detail/Result.h"

namespace oxygen::co::detail {

//! An adapter which wraps an awaitable to provide a standardized interface.
/*!
 This adapter augments and sanitizes an awaitable object in 3 ways:
   - its await_suspend() always returns a coroutine_handle<>;
   - its await_resume() always returns something which can be stored in a local
     variable or stuffed into std::variant or std::tuple;
   - provides possibly-dummy versions of all optional await_*() methods:
     await_set_executor, await_early_cancel, await_cancel, await_must_resume,
     await_introspect

 Many of the 'standardized' implementations for individual await_foo() methods
 are available as `detail::AwaitFoo()` also.

\see GetAwaitable()
*/
template <class Aw>
class AwaitableAdapter {
    static_assert(DirectAwaitable<Aw>,
        "AwaitableAdapter must be initialized with a `DirectAwaitable` (i.e. "
        "one which implements the `await_...` methods); pass your object "
        "through `GetAwaitable()`.");

    using Ret = decltype(std::declval<Aw>().await_resume());

public:
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    explicit AwaitableAdapter(Aw&& awaitable)
        : awaitable_(StaticAwaitableCheck<Aw&&>(std::forward<Aw>(awaitable)))
    {
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
        return checker_.ReadyReturned(awaitable_.await_ready());
    }

    [[nodiscard]] auto await_suspend(Handle h) -> Handle
    {
#if defined(OXCO_AWAITABLE_STATE_DEBUG)
        try {
            return AwaitSuspend(awaitable_, checker_.AboutToSuspend(h));
        } catch (...) {
            checker_.SuspendThrew();
            throw;
        }
#else
        return AwaitSuspend(awaitable_, h);
#endif
    }

    auto await_resume() -> decltype(auto)
    {
        checker_.AboutToResume();
        if constexpr (std::is_same_v<Ret, void>) {
            std::forward<Aw>(awaitable_).await_resume();
            return Void {};
        } else {
            return std::forward<Aw>(awaitable_).await_resume();
        }
    }

    auto await_early_cancel() noexcept
    {
        return checker_.EarlyCancelReturned(AwaitEarlyCancel(awaitable_));
    }

    auto await_cancel(const Handle h) noexcept
    {
        return checker_.CancelReturned(
            AwaitCancel(awaitable_, checker_.AboutToCancel(h)));
    }

    [[nodiscard]] auto await_must_resume() const noexcept
    {
        return checker_.MustResumeReturned(AwaitMustResume(awaitable_));
    }

    void await_set_executor(Executor* ex) noexcept
    {
        checker_.AboutToSetExecutor();
        if constexpr (NeedsExecutor<Aw>) {
            awaitable_.await_set_executor(ex);
        }
    }

    // Used by `Runner::Run()` if the event loop stops before the
    // awaitable completes. Disables the awaitable checker (if any),
    // allowing the awaitable to be destroyed even in states where it
    // shouldn't be.
    void Abandon() { checker_.ForceReset(); }

private:
    [[no_unique_address]] AwaitableStateChecker checker_;
    Aw awaitable_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

} // namespace oxygen::co::detail

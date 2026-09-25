//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/AwaitFn.h>
#include <Oxygen/OxCo/Detail/AwaiterStateChecker.h>
#include <Oxygen/OxCo/Detail/Result.h>

namespace oxygen::co::detail {

//! An adapter which wraps an awaitable to provide a standardized interface.
/*!
 This adapter augments and sanitizes an awaitable object in 3 ways:
   - its await_suspend() always returns a coroutine_handle<>;
   - its await_resume() always returns something which can be stored in a local
     variable or stuffed into std::variant or std::tuple;
   - provides possibly-dummy versions of all optional await_*() methods:
     await_set_executor, await_early_cancel, await_cancel, await_must_resume,...

 Many of the 'standardized' implementations for individual await_foo() methods
 are available as `detail::AwaitFoo()` also.

\see GetAwaiter()
*/
template <class T, class Awaiter = AwaiterType<T>> class SanitizedAwaiter {
  using Ret = decltype(std::declval<Awaiter>().await_resume());

public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  explicit SanitizedAwaiter(T&& object)
    : awaiter_(GetAwaiter<T>(std::forward<T>(object)))
  {
  }

  [[nodiscard]] auto await_ready() const noexcept -> bool
  {
    return checker_.ReadyReturned(awaiter_.await_ready());
  }

  [[nodiscard]] auto await_suspend(Handle h) -> Handle
  {
#if defined(OXCO_AWAITER_STATE_DEBUG)
    try {
      return AwaitSuspend(awaiter_, checker_.AboutToSuspend(h));
    } catch (...) {
      checker_.SuspendThrew();
      throw;
    }
#else
    return AwaitSuspend(awaiter_, h);
#endif
  }

  auto await_resume() -> decltype(auto)
  {
    checker_.AboutToResume();
    if constexpr (std::is_same_v<Ret, void>) {
      std::forward<Awaiter>(awaiter_).await_resume();
      return Void {};
    } else {
      return std::forward<Awaiter>(awaiter_).await_resume();
    }
  }

  auto await_early_cancel() noexcept
  {
    return checker_.EarlyCancelReturned(AwaitEarlyCancel(awaiter_));
  }

  auto await_cancel(const Handle h) noexcept
  {
    const auto result = AwaitCancel(awaiter_, checker_.AboutToCancel(h));
    if constexpr (!Cancellable<Awaiter>) {
      // The normalized interface must remain Cancellable: std::false_type is
      // not an allowed await_cancel return type. Otherwise type-erased callers
      // skip this method and the checker never observes the cancellation.
      return checker_.CancelReturned(static_cast<bool>(result));
    } else {
      return checker_.CancelReturned(result);
    }
  }

  [[nodiscard]] auto await_must_resume() const noexcept
  {
    const auto result = AwaitMustResume(awaiter_);
    if constexpr (std::is_same_v<std::remove_cv_t<decltype(result)>,
                    std::true_type>) {
      // Like await_cancel, expose a return type accepted by the public concept.
      // Keep std::false_type where it conveys that results are discardable.
      return checker_.MustResumeReturned(static_cast<bool>(result));
    } else {
      return checker_.MustResumeReturned(result);
    }
  }

  void await_set_executor(Executor* ex) noexcept
  {
    checker_.AboutToSetExecutor();
    if constexpr (NeedsExecutor<Awaiter>) {
      awaiter_.await_set_executor(ex);
    }
  }

  // Used by `Runner::Run()` if the event loop stops before the
  // awaitable completes. Disables the awaitable checker (if any),
  // allowing the awaitable to be destroyed even in states where it
  // shouldn't be.
  void Abandon() { checker_.ForceReset(); }

private:
  [[no_unique_address]] AwaiterStateChecker checker_;
  Awaiter awaiter_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

template <class T> SanitizedAwaiter(T&&) -> SanitizedAwaiter<T>;

template <class T, class... Args> class AwaiterMaker {
public:
  explicit AwaiterMaker(Args&&... args)
    : args_(std::forward<Args>(args)...)
  {
  }

  auto operator co_await() && -> T
  {
    return std::make_from_tuple<T>(std::move(args_));
  }

private:
  std::tuple<Args...> args_;
};

} // namespace oxygen::co::detail

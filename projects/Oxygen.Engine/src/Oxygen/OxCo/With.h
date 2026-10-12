//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/With.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::co {

//! Runs one awaitable inside the lifetime of another, supporting awaitable
//! (to some degree, an equivalent of Python's `async with` statement).
/*!
 More specifically:
   - `first` is invoked with a `TaskStarted<T>` argument, and the awaitable it
     returns is started;
   - once `first` signals readiness by invoking its `TaskStarted<T>`, `then` is
     invoked (with the value passed to `TaskStarted<T>`, if any) and runs
     concurrently with `first`;
   - when `then` completes, its result is stored, its awaitable is destroyed
     (running destructors and scope guards), and only then is `first`
     cancelled;
   - if cancelled externally, `then` is cancelled first; `first` is cancelled
     only after `then` has fully completed, including asynchronous cleanup;
   - the result of the whole operation is the result of `then`.

 `first` must not complete on its own unless it throws; the exception then
 propagates after `then` is cancelled. Completing normally is a contract
 violation and aborts. If `first` already has a pending cancellation when it
 calls `started()`, `then` never runs. `With` is immediately cancellable only
 if both stages are.

 This suits live objects whose usability depends on a background task, and
 which must stay alive until every user has finished, including users that are
 being cancelled:

 \code{cpp}
   struct MyObject {
     auto Run() {
       return [this](co::TaskStarted<int> started) -> co::Co<> {
         // ...async initialization...
         started(42);
         co_await co::AnyOf(
           [&]() -> co::Co<> {
             for (;;) { co_await Bookkeeping(); }
           },
           co::UntilCancelledAnd([&]() -> co::Co<> {
             // ...asynchronous cleanup...
           }));
       };
     }
   };

   co::Co<> Use() {
     MyObject obj;
     co_await co::With(obj.Run(), [&](int v) -> co::Co<> {
       // use obj; `obj.Run()` is not cancelled until this body has finished,
       // including any asynchronous cleanup of its own.
       co_return;
     });
   }
 \endcode

 Unlike `OXCO_WITH_NURSERY(n) { n.Start(...); co_await Body(); }`, where the
 supporting task may observe cancellation before the body does, `With`
 guarantees that the body is fully cancelled first.

 \see OXCO_WITH for the block syntax.
*/
template <class FirstFn, class ThenFn>
  requires(std::invocable<FirstFn, detail::TaskStartedTag>
    && Awaitable<std::invoke_result_t<FirstFn, detail::TaskStartedTag>>
    && Awaitable<
      typename detail::ChainableForWith<FirstFn, ThenFn>::ResultType>)
auto With(FirstFn&& first, ThenFn&& then) -> Awaitable auto
{
  return MakeAwaitable<detail::With<FirstFn, ThenFn>, FirstFn, ThenFn>(
    std::forward<FirstFn>(first), std::forward<ThenFn>(then));
}

} // namespace oxygen::co

//! Block syntax for `co::With()`, usable inside a coroutine:
/*!
 \code{cpp}
   OXCO_WITH(obj.Run(), int v) {
     // same as the body of co::With(obj.Run(), [&](int v) -> co::Co<> {...})
   };
 \endcode
*/
// The block syntax needs a macro; no function can expand to `co_yield`.
// NOLINTBEGIN(bugprone-macro-parentheses, cppcoreguidelines-macro-usage)
#define OXCO_WITH(first, ...)                                                  \
  co_yield ::oxygen::co::detail::WithBuilder(first)                            \
    % [&](__VA_ARGS__)->::oxygen::co::Co<>
// NOLINTEND(bugprone-macro-parentheses, cppcoreguidelines-macro-usage)

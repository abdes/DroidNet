//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/CancellableAdapter.h"
#include "Oxygen/OxCo/Detail/CoAwaitFactory.h"
#include "Oxygen/OxCo/Detail/DisposableAdapter.h"
#include "Oxygen/OxCo/Detail/ReadyAwaiter.h"
#include "Oxygen/OxCo/Detail/Sequence.h"
#include "Oxygen/OxCo/SuspendForever.h"
#include "Oxygen/OxCo/Yield.h"

namespace oxygen::co {

//! Create a task that immediately returns a given value when `co_await`'ed.
template <class T>
Awaitable<T> auto Just(T value) // NOLINT(modernize-use-trailing-return-type)
{
    return detail::ReadyAwaiter<T>(std::forward<T>(value));
}

//! A no-op task. Always await_ready(), and `co_await`ing on it is a no-op
//! either (i.e. immediately resumes the caller).
/*!
 Can be useful when defining interfaces having optional methods:
 \code{.cpp}
     struct IExample {
         virtual Co<> OptionalToImplement() {
             return co::NoOp();
         }
     };
 \endcode

 Saving on coroutine frame allocation (compared to `{ co_return; }`).
*/
inline Awaitable<void> auto NoOp() // NOLINT(modernize-use-trailing-return-type)
{
    return detail::ReadyAwaiter<void>();
}

static constexpr detail::CoAwaitFactory<SuspendForever> kSuspendForever;
static constexpr detail::CoAwaitFactory<Yield> kYield;

/// A utility function which allows delayed construction of nonmoveable
/// immediate awaitables.
///
/// The returned class is moveable (assuming the arguments are moveable),
/// and has a one-shot `operator co_await() &&`, which will construct
/// `T(forward<Args>(args...))` and return it.
template <ImmediateAwaitable T, class... Args>
Awaitable auto MakeAwaitable(Args&&... args)
{
    return detail::AwaiterMaker<T, Args...>(std::forward<Args>(args)...);
}

//! A wrapper around an awaitable suppressing its cancellation.
/*!
 \code{cpp}
   // If this line gets executed...
   co_await NonCancellable([]() -> Task<> {
       co_await Sleep(10_s);
       // ... this line is guaranteed to get executed as well
       //     (assuming Sleep doesn't throw an exception)
   });
   // ... and so is this one
 \endcode
*/
template <class Awaitable>
auto NonCancellable(Awaitable awaitable)
{
    return MakeAwaitable<detail::NonCancellableAdapter<Awaitable>>(
        std::forward<Awaitable>(awaitable));
}

//! A wrapper around an awaitable declaring that its return value is safe to
//! dispose of upon cancellation.
/*!
 May be used on third party awaitables which don't know about the async
 cancellation mechanism. Note that this won't result in the awaitable completing
 any faster when cancelled; it only affects what happens _after the awaitable
 completes_ when a cancellation has been requested.
*/
template <class Awaitable>
auto Disposable(Awaitable awaitable)
{
    return MakeAwaitable<detail::DisposableAdapter<Awaitable>>(
        std::forward<Awaitable>(awaitable));
}

//! Chains multiple awaitables together, without having to allocate a coroutine
//! frame.
/*!
 \tparam ThenFn a `std::invocable` (a callable type that can be called with a
 set of arguments Args... using the function template `std::invoke`.) that
 takes the result of the previous awaitable and returns a new awaitable.

 \param then_fn the chained invocable, must be a `std::invocable` that either
 takes no arguments or takes one argument representing the result of the
 previous awaitable (by value or by lvalue- or rvalue-reference) and returns a
 new awaitable.

 Lifetime of the result of the previous awaitable is extended until the next
 awaitable completes, allowing the following:
 \code{cpp}
    class My {
      co::Semaphore sem_;
    public:
      Awaitable<void> auto DoSmth() {
        return sem_.lock() | co::Then([]{
          ReallyDoSmth();
          return co::NoOp();
        }
      }
    };
 \endcode

 -- which would be a more efficient equivalent of
 \code{cpp}
    Co<> DoSmth() {
      co_await sem_.lock();
      ReallyDoSmth();
    }
 \endcode

 Multiple `Then()` can be chained together, but using a coroutine might yield
 better readability in such cases. Also keep in mind that lifetime extension
 only spans until the next awaitable completes, so
 \code{cpp}
    return sem_.lock() | co::Then(doThis) | co::Then(doThat);
 \endcode

 -- is roughly equivalent to
 \code{cpp}
    { co_await sem_.lock(); co_await DoThis(); }
    co_await DoThat();
 \endcode

 -- and is therefore different from
 \code{cpp}
     return sem_.lock() | co::Then([]{
       return DoThis | co::Then(DoThat);
     });
 \endcode
*/
template <class ThenFn>
auto Then(ThenFn&& then_fn)
{
    return detail::SequenceBuilder<ThenFn>(std::forward<ThenFn>(then_fn));
}

} // namespace oxygen::co

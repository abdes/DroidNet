//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Awaitables.h"
#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/MuxRange.h"
#include "Oxygen/OxCo/Detail/MuxTuple.h"
#include "Oxygen/OxCo/Detail/RunOnCancel.h"

namespace oxygen::co {

//! Return an awaitable which runs the given callable and then resumes the
//! caller immediately, evaluating to the result thereof.
template <class Callable>
auto YieldToRun(Callable cb)
{
    return detail::YieldToRunAwaitable<Callable>(std::move(cb));
}

//! A wrapper that adapts an awaitable so it runs upon cancellation instead of
//! immediately. This is the primary suggested method for implementing
//! asynchronous cleanup logic, since C++ doesn't support destructors that are
//! coroutines.
/*!
 The awaitable will be started when cancellation is requested, and cancellation
 will be confirmed when the awaitable completes. The awaitable will not itself
 execute in a cancelled context -- you can do blocking operations normally --
 although it is allowed to use cancellation internally. Consider attaching a
 timeout if you need to do something that might get stuck.

 This can be used as an async equivalent of a scope guard, to attach async
 cleanup logic to an async operation:
 \code{cpp}
    co_await AnyOf(
        DoSomething(),
        untilCancelledAnd([]() -> Co<> {
            co_await DoAsyncCleanup();
        }));
 \endcode

 Make sure not to throw any exceptions from the awaitable, as they will
 `terminate()` the process.
*/
template <Awaitable<void> Aw>
auto UntilCancelledAnd(Aw&& awaitable)
{
    return MakeAwaitable<detail::RunOnCancel<Aw>>(std::forward<Aw>(awaitable));
}

//! Run multiple awaitables concurrently. Upon completion of any one, request
//! cancellation of the rest; once all are finished executing, return the
//! result(s) of the awaitable(s) that completed normally.
/*!
 Returns a `std::tuple<Optional<R>...>` of the awaitable return types, as
 multiple awaitables may complete at the same time.

 If any of the awaitables would return void, it will be replaced by
 `co::detail::Void` type, so the resulting type would still compile. This
 applies even if all awaitables return void, so one can use
 `std::get<N>(result).has_value()` to figure out which awaitable(s) completed.
*/
template <Awaitable... Ts>
auto AnyOf(Ts&&... awaitables) -> Awaitable auto
{
    static_assert(
        sizeof...(Ts) == 0
            || (detail::Cancellable<detail::AwaiterType<Ts>> || ...),
        "AnyOf() makes no sense if all awaitables are non-cancellable");

    return MakeAwaitable<detail::AnyOfMux<Ts...>>(std::forward<Ts>(awaitables)...);
}

//! Run multiple awaitables concurrently, but for variable-length ranges of
//! awaitables.
//! \see AnyOf(Ts&&...)
template <AwaitableRange<> Range>
auto AnyOf(Range&& range) -> Awaitable auto
{
    return MakeAwaitable<detail::AnyOfRange<Range>>(std::forward<Range>(range));
}

//! Run multiple awaitables concurrently; once all of them complete or are
//! cancelled, return a tuple of the available results.
/*!
 Upon cancellation, proxies cancellation to all the awaitables; if some of them
 complete before cancellation and others get cancelled, may return a partial
 result. Hence, returns a `std::tuple<Optional<R>...>`.
*/
template <Awaitable... Ts>
auto MostOf(Ts&&... awaitables) -> Awaitable auto
{
    return MakeAwaitable<detail::MostOfMux<Ts...>>(std::forward<Ts>(awaitables)...);
}

//! Run multiple awaitables concurrently with `MostOf`, but for variable-length
//! ranges of awaitables.
//! \see MostOf(Ts&&...)
template <AwaitableRange<> Range>
auto MostOf(Range&& range) -> Awaitable auto
{
    return MakeAwaitable<detail::MostOfRange<Range>>(std::forward<Range>(range));
}

//! Run multiple awaitables concurrently; once all of them complete, return a
//! `std::tuple<R...>` of the results.
/*!
 If cancellation occurs before all awaitables complete, the results
 of the awaitables that did complete before the cancellation may be
 discarded. If that's not desirable, use `co::MostOf()` instead.
*/
template <Awaitable... Ts>
auto AllOf(Ts&&... awaitables) -> Awaitable auto
{
    return MakeAwaitable<detail::AllOfMux<Ts...>>(std::forward<Ts>(awaitables)...);
}

//! Run multiple awaitables concurrently with `AllOf`, but for variable-length
//! ranges of awaitables.
//! \see AllOf(Ts&&...)
template <AwaitableRange<> Range>
auto AllOf(Range&& range) -> Awaitable auto
{
    return MakeAwaitable<detail::AllOfRange<Range>>(std::forward<Range>(range));
}

} // namespace oxygen::co

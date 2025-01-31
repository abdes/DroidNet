//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include "Oxygen/OxCo/Concepts/Awaiter.h"

namespace oxygen::co {

namespace detail {
    //! Required internally to circumvent certain out-of-order definitions
    //! of `operator co_await()`.
    template <class T, class Ret>
    constexpr bool kThisIsAwaitableTrustMe = false;
} // namespace oxygen::co::detail

//! Defined the requirements of an Awaitable type obtained directly from the
//! `expr` in `co_await expr` when `expr` is produced by an initial suspend
//! point, a final suspend point, or a yield expression.
template <typename T, class Ret = detail::Unspecified>
concept DirectAwaitable = Awaiter<T, Ret>;

template <typename T, class Ret = detail::Unspecified>
concept MemberCoAwaitAwaitable = requires(T t) {
    { std::forward<T>(t).operator co_await() } -> DirectAwaitable<Ret>;
};

template <typename T, class Ret = detail::Unspecified>
concept GlobalCoAwaitAwaitable = requires(T t) {
    { operator co_await(std::forward<T>(t)) } -> DirectAwaitable<Ret>;
};

//! Defined the requirements of the general Awaitable type, including through
//! the member function `await_transform` of the coroutine `Promise` type.
/*!
 If the current coroutine `Promise` type has a member function `await_transform`,
 then the `awaitable` is obtained by calling `promise.await_transform(expr)`. This
 allows the coroutine to customize how expressions are transformed into
 `awaitable`.
*/
template <typename T, class Ret = detail::Unspecified>
concept Awaitable
    = DirectAwaitable<T>
    || MemberCoAwaitAwaitable<T>
    || GlobalCoAwaitAwaitable<T>
    || detail::kThisIsAwaitableTrustMe<T, Ret>;

template <class R, class Ret = detail::Unspecified>
concept AwaitableRange = requires(R r) {
    { r.begin() == r.end() } -> std::convertible_to<bool>;
    { *r.begin() } -> Awaitable<Ret>;
};

template <typename type>
concept MemberCoAwaitAwaitableVoid = requires(type t) {
    { t.operator co_await() } -> AwaiterVoid;
};

template <typename type>
concept GlobalCoAwaitAwaitableVoid = requires(type t) {
    { operator co_await(t) } -> AwaiterVoid;
};

template <typename type>
concept AwaitableVoid = MemberCoAwaitAwaitableVoid<type>
    || GlobalCoAwaitAwaitableVoid<type>
    || AwaiterVoid<type>;

//! @{
//! Extensions to the Awaitable concept.

class Executor;

//! An awaitable that conforms to NeedsExecutor<T> defines an
//! `await_set_executor(Executor*)` method, which will be called before
//! `await_suspend()` when the awaitable is awaited.
/*!
 It can be used to obtain a pointer to the current executor, which is useful to
 control scheduling for the awaitable itself or to propagate it to others
 that might need it.
 */
template <class T>
concept NeedsExecutor = requires(T t, Executor* ex) {
    { t.await_set_executor(ex) } noexcept -> std::same_as<void>;
};

//! @}

//! @{
//! Helper traits for awaiter and awaitable.

template <Awaitable awaitable, typename = void>
struct awaitable_traits {
};

template <Awaitable awaitable>
static auto GetAwaiter(awaitable&& value)
{
    if constexpr (MemberCoAwaitAwaitable<awaitable>)
        return std::forward<awaitable>(value).operator co_await();
    else if constexpr (GlobalCoAwaitAwaitable<awaitable>)
        return operator co_await(std::forward<awaitable>(value));
    else if constexpr (Awaiter<awaitable>) {
        return std::forward<awaitable>(value);
    }
}

template <Awaitable awaitable>
struct AwaitableTraits {
    using AwaiterType = decltype(GetAwaiter(std::declval<awaitable>()));
    using AwaiterReturnType = decltype(std::declval<AwaiterType>().await_resume());
};

//! @}

} // namespace oxygen::co

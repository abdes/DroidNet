//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <coroutine>

namespace oxygen::co {

namespace detail {

    // Utility helpers for concepts below.
    template <class From, class... To>
    concept ConvertibleToAny = (std::convertible_to<From, To> || ...);

    template <class From, class... To>
    concept SameAsAny = (std::same_as<From, To> || ...);

    struct Unspecified { };

} // namespace oxygen::co::detail

//! Defined the requirements of an Awaiter type as expected by the C++20
//! coroutine specification.
/*!
 An `Awaiter` is an object that interacts directly with the coroutine mechanism
 to control the suspension and resumption of the coroutine. It's obtained from
 the `Awaitable` by calling the operator `co_await` on it.

 The awaiter provides specific methods:

   - `await_ready()->bool`: Determines whether the coroutine should be
     suspended.

   - `await_suspend(std::coroutine_handle<>)->void|bool|std::coroutine_handle<>`:
     Specifies the action to take when the coroutine is suspended.
       - If the return type is `void`, control is immediately returned to the
         caller of the current coroutine (this coroutine remains suspended),
       - If the return type is `bool`, 'true' returns control to the caller of
         the current coroutine, 'false' resumes the current coroutine.
       - If the return type is `std::coroutine_handle<>`, the handle is resumed
         (by a call to handle.resume(), which may eventually chain to resuming
         the current coroutine).

   - `await_resume()`: Called (whether the coroutine was suspended or not), and
     its result is the result of the whole `co_await expr` expression.
*/
// clang-format off
template<typename T, class Ret = detail::Unspecified>
concept Awaiter = requires(T t, const T ct, std::coroutine_handle<> h)
{
    { t.await_ready() } -> std::same_as<bool>;
    { t.await_suspend(h) } -> detail::ConvertibleToAny<void, bool, std::coroutine_handle<>>;
    { std::forward<T>(t).await_resume() };
}
&& (std::is_same_v<Ret, detail::Unspecified>
    || requires(T t) {
        { std::forward<T>(t).await_resume() } -> std::convertible_to<Ret>;
    });


template <typename type>
concept AwaiterVoid = Awaiter<type> && requires(type t) {
    { t.await_resume() } -> std::same_as<void>;
};

} // namespace oxygen::co

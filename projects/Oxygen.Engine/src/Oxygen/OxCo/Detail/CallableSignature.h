//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace oxygen::co::detail {

// CallableSignature<Fn> is a structure containing the following alias
// declarations:
//
// - Ret: the return type of the callable object represented by Fn
//   (like std::invoke_result_t, but we don't need to specify the argument
//   types because we assume no overloading or template usage for operator())
//
// - Arity: the number of arguments of the callable;
//
// - Arg<I>: the type of the I-th argument of the callable;
//
// - BindArgs<T>: the type T<Args...> where Args... are the arguments
//   of the callable (T is any template).
template <class Fn>
struct CallableSignature;

template <class R, class S, class... Args>
struct CallableSignature<R (S::*)(Args...)> {
    static constexpr bool IsMemFunPtr = true;
    static constexpr size_t Arity = sizeof...(Args);

    template <size_t I>
    using Arg = std::tuple_element_t<I, std::tuple<Args...>>;

    template <template <class...> class T>
    using BindArgs = T<Args...>;
    using Ret = R;
};

template <class R, class S, class... Args>
struct CallableSignature<R (S::*)(Args...) noexcept>
    : CallableSignature<R (S::*)(Args...)> { };

template <class R, class S, class... Args>
struct CallableSignature<R (S::*)(Args...) const>
    : CallableSignature<R (S::*)(Args...)> { };

template <class R, class S, class... Args>
struct CallableSignature<R (S::*)(Args...) const noexcept>
    : CallableSignature<R (S::*)(Args...)> { };

template <class R, class... Args>
struct CallableSignature<R (*)(Args...)> {
    static constexpr bool IsMemFunPtr = false;
    static constexpr size_t Arity = sizeof...(Args);
    template <size_t I>
    using Arg = std::tuple_element_t<I, std::tuple<Args...>>;
    template <template <class...> class T>
    using BindArgs = T<Args...>;
    using Ret = R;
};
template <class R, class... Args>
struct CallableSignature<R (*)(Args...) noexcept>
    : CallableSignature<R (*)(Args...)> { };

template <class T>
struct CallableSignature<T&&> : CallableSignature<std::remove_cvref_t<T>> { };

template <class Fn>
struct CallableSignature : CallableSignature<decltype(&Fn::operator())> {
    static constexpr bool IsMemFunPtr = false;
};

} // namespace oxygen::co::detail

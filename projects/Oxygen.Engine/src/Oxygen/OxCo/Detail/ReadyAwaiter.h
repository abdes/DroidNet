//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Coroutine.h"

#include <utility>

namespace oxygen::co::detail {

/// A non-cancellable awaitable which is immediately ready, producing a
/// value of type T. It can also be implicitly converted to a `Co<T>`.
template <class T>
class ReadyAwaiter {
public:
    // No move, we want a reference to the value.
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    explicit ReadyAwaiter(T&& value)
        : value_(std::forward<T>(value))
    {
    }

    //! @{
    //! Implementation of the awaiter interface.
    // ReSharper disable CppMemberFunctionMayBeStatic
    // NOLINTBEGIN(*-convert-member-functions-to-static, *-use-nodiscard)

    auto await_early_cancel() const noexcept { return false; }
    auto await_ready() const noexcept { return true; }
    auto await_suspend(Handle /*unused*/) { return false; }
    auto await_cancel(Handle /*unused*/) noexcept { return false; }
    auto await_must_resume() const noexcept { return true; }
    auto await_resume() && -> T { return std::forward<T>(value_); }

    // ReSharper disable CppMemberFunctionMayBeStatic
    // NOLINTEND(*-convert-member-functions-to-static, *-use-nodiscard)
    //! @}

    // ReSharper disable once CppNonExplicitConversionOperator
    template <std::constructible_from<T> U>
    // NOLINTNEXTLINE(*-explicit-constructor, *-explicit-conversions)
    operator Co<U>() &&
    {
        return Co<U>(*new StubPromise<U>(std::forward<T>(value_)));
    }

private:
    T value_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

template <>
class ReadyAwaiter<void> {
public:
    //! @{
    //! Implementation of the awaiter interface.
    // ReSharper disable CppMemberFunctionMayBeStatic
    // NOLINTBEGIN(*-convert-member-functions-to-static, *-use-nodiscard)

    auto await_early_cancel() const noexcept { return false; }
    auto await_ready() const noexcept { return true; }
    auto await_suspend(Handle /*unused*/) { return false; }
    auto await_cancel(Handle /*unused*/) noexcept { return false; }
    auto await_must_resume() const noexcept { return true; }
    auto await_resume() const noexcept { }

    // ReSharper disable CppMemberFunctionMayBeStatic
    // NOLINTEND(*-convert-member-functions-to-static, *-use-nodiscard)
    //! @}

    // ReSharper disable once CppNonExplicitConversionOperator
    // NOLINTNEXTLINE(*-explicit-constructor, *-explicit-conversions)
    operator Co<>() const { return Co(StubPromise<void>::Instance()); }
};

} // namespace oxygen::co::detail

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
class ReadyAwaitable {
public:
    // No move, we want a reference to the value.
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    explicit ReadyAwaitable(T&& value)
        : value_(std::forward<T>(value))
    {
    }

    [[nodiscard]] static auto await_early_cancel() noexcept { return false; }
    [[nodiscard]] static auto await_ready() noexcept { return true; }
    [[nodiscard]] static auto await_suspend(Handle /*unused*/) { return false; }
    [[nodiscard]] static auto await_cancel(Handle /*unused*/) { return false; }
    [[nodiscard]] static auto await_must_resume() noexcept { return true; }
    [[nodiscard]] auto await_resume() && -> T { return std::forward<T>(value_); }

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
class ReadyAwaitable<void> {
public:
    [[nodiscard]] static auto await_early_cancel() noexcept { return false; }
    [[nodiscard]] static auto await_ready() noexcept { return true; }
    [[nodiscard]] static auto await_suspend(Handle /*unused*/) { return false; }
    [[nodiscard]] static auto await_cancel(Handle /*unused*/) { return false; }
    [[nodiscard]] static auto await_must_resume() noexcept { return true; }
    [[nodiscard]] static auto await_resume() { }

    // ReSharper disable once CppNonExplicitConversionOperator
    // NOLINTNEXTLINE(*-explicit-constructor, *-explicit-conversions)
    operator Co<>() const { return Co(StubPromise<void>::Instance()); }
};

} // namespace oxygen::co::detail

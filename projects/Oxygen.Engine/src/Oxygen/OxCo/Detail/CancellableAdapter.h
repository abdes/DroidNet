//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/GetAwaitable.h"

namespace oxygen::co::detail {

//! A common part for `NonCancellable()` and `Disposable()`.
template <class T>
class CancellableAdapter {
public:
    // ReSharper disable CppMemberFunctionMayBeStatic
    explicit CancellableAdapter(T&& object) // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        : object_(std::forward<T>(object))
        , awaitable_(GetAwaitable(std::forward<T>(object_)))
    {
    }

    void await_set_executor(Executor* ex) noexcept
    {
        if constexpr (NeedsExecutor<AwaitableType<T>>) {
            awaitable_.await_set_executor(ex);
        }
    }

    [[nodiscard]] auto await_ready() const noexcept { return awaitable_.await_ready(); }
    [[nodiscard]] auto await_early_cancel() noexcept { return false; }
    [[nodiscard]] auto await_suspend(Handle h) { return awaitable_.await_suspend(h); }
    [[nodiscard]] auto await_must_resume() const noexcept { return true; }
    auto await_resume() & -> decltype(auto) { return awaitable_.await_resume(); }
    auto await_resume() && -> decltype(auto)
    {
        return std::move(awaitable_).await_resume();
    }
    // ReSharper restore CppMemberFunctionMayBeStatic

protected:
    T object_; // NOLINT(*-non-private-member-variables-in-classes)
    AwaitableType<T> awaitable_; // NOLINT(*-non-private-member-variables-in-classes)
};

} // namespace oxygen::co::detail

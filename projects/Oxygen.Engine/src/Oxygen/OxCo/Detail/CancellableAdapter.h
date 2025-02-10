//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/AwaitFn.h"
#include "Oxygen/OxCo/Detail/GetAwaitable.h"

namespace oxygen::co::detail {

// A common part of NonCancellableAdapter and DisposableAdapter.
// Note: all three are meant to be used together with AwaitableMaker,
// so they don't store the object they have been passed.
template <class T>
class CancellableAdapterBase {
protected:
    using Aw = AwaitableType<T>;
    Aw awaitable_;

public:
    explicit CancellableAdapterBase(T&& object)
        : awaitable_(GetAwaitable(std::forward<T>(object)))
    {
    }

    void await_set_executor(Executor* ex) noexcept
    {
        AwaitSetExecutor(awaitable_, ex);
    }

    bool await_ready() const noexcept { return awaitable_.await_ready(); }
    auto await_suspend(Handle h) { return awaitable_.await_suspend(h); }
    decltype(auto) await_resume()
    {
        return std::forward<Aw>(awaitable_).await_resume();
    }
};

/// A wrapper around an awaitable that inhibits cancellation.
template <class T>
class NonCancellableAdapter : public CancellableAdapterBase<T> {
public:
    using CancellableAdapterBase<T>::CancellableAdapterBase;

    bool await_early_cancel() noexcept { return false; }
    bool await_must_resume() const noexcept { return true; }
};

} // namespace oxygen::co::detail

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "CancellableAdapter.h"

namespace oxygen::co::detail {

//! A wrapper around an awaitable declaring that its return value is safe to
//! dispose of upon cancellation. May be used on third party awaitables which
//! don't know about the async cancellation mechanism.
template <class T>
class DisposableAdapter : public CancellableAdapterBase<T> {
public:
    using CancellableAdapterBase<T>::CancellableAdapterBase;

    bool await_early_cancel() noexcept
    {
        return AwaitEarlyCancel(this->awaitable_);
    }
    bool await_cancel(Handle h) noexcept
    {
        return AwaitCancel(this->awaitable_, h);
    }
    // ReSharper disable once CppMemberFunctionMayBeStatic
    auto await_must_resume() const noexcept { return std::false_type {}; }
};

} // namespace oxygen::co::detail

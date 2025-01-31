
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <coroutine>

namespace oxygen::co {

//! Coroutine handle type.
template <class Promise>
using CoroutineHandle = std::coroutine_handle<Promise>;

namespace detail {
    //! Type erased coroutine handle, for internal use only.
    using Handle = CoroutineHandle<void>;
} // namespace oxygen::co::detail

//! Similar to std::noop_coroutine(), but guaranteed to return the same value
//! for each invocation, so can be compared against.
inline auto NoOpHandle()
{
    static const detail::Handle ret = std::noop_coroutine();
    return ret;
}

} // namespace oxygen::co

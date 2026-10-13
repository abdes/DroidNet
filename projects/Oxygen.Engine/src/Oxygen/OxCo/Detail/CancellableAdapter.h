//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/AwaitFn.h>
#include <Oxygen/OxCo/Detail/GetAwaiter.h>

namespace oxygen::co::detail {

// A common part of NonCancellableAdapter and DisposableAdapter.
// Note: all three are meant to be used together with AwaiterMaker,
// so they don't store the object they have been passed.
template <class T> class CancellableAdapterBase {
protected:
  using Awaiter = AwaiterType<T>;
  Awaiter awaiter_;

public:
  explicit CancellableAdapterBase(T&& object)
    : awaiter_(GetAwaiter(std::forward<T>(object)))
  {
  }

  void await_set_executor(Executor* ex) noexcept
  {
    AwaitSetExecutor(awaiter_, ex);
  }

  auto await_ready() const noexcept -> bool { return awaiter_.await_ready(); }
  auto await_suspend(Handle h) { return awaiter_.await_suspend(h); }
  auto await_resume() -> decltype(auto)
  {
    return std::forward<Awaiter>(awaiter_).await_resume();
  }
};

/// A wrapper around an awaitable that inhibits cancellation.
template <class T>
class NonCancellableAdapter : public CancellableAdapterBase<T> {
public:
  using CancellableAdapterBase<T>::CancellableAdapterBase;

  //! @{
  //! Implementation of the awaiter interface.
  // NOLINTBEGIN(*-use-nodiscard)

  auto await_early_cancel() noexcept -> bool { return false; }
  auto await_must_resume() const noexcept -> bool { return true; }

  // NOLINTEND(*-use-nodiscard)
  //! @}
};

} // namespace oxygen::co::detail

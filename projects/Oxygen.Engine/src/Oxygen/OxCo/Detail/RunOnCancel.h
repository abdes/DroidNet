//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/SanitizedAwaiter.h>

#include <utility>

namespace oxygen::co {

class Executor;

namespace detail {
  /// A utility class kicking off an awaitable upon cancellation.
  template <class Awaitable> class RunOnCancel {
  public:
    explicit RunOnCancel(Awaitable&&
        awaitable) // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
      : awaiter_(std::forward<Awaitable>(awaitable))
    {
    }

    // ReSharper disable CppMemberFunctionMayBeStatic
    void await_set_executor(Executor* ex) noexcept
    {
      awaiter_.await_set_executor(ex);
    }

    [[nodiscard]] auto await_ready() const noexcept { return false; }
    auto await_early_cancel() noexcept -> bool
    {
      cancel_pending_ = true;
      return false;
    }
    void await_suspend(const Handle h)
    {
      if (cancel_pending_) {
        this->await_cancel(h);
      }
    }
    [[noreturn]] void await_resume() { Unreachable(); }

    auto await_cancel(Handle h) noexcept -> bool
    {
      // If the awaitable immediately resumes, then this is sort of
      // like a synchronous cancel, but we still need to structure
      // it as "resume the handle ourselves, then return false" in
      // order to make sure await_must_resume() gets called to check
      // for exceptions.
      if (awaiter_.await_ready()) {
        h.resume();
      } else {
        awaiter_.await_suspend(h).resume();
      }
      return false;
    }
    auto await_must_resume() const noexcept
    {
      awaiter_.await_resume(); // terminate() on any pending exception
      return std::false_type {};
    }
    // ReSharper restore CppMemberFunctionMayBeStatic

  protected:
    // NOLINTNEXTLINE(*-non-private-member-variables-in-classes)
    [[no_unique_address]] mutable SanitizedAwaiter<Awaitable> awaiter_;
    // NOLINTNEXTLINE(*-non-private-member-variables-in-classes)
    bool cancel_pending_ = false;
  };

} // namespace detail

} // namespace oxygen::co

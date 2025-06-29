//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/OxCo/Coroutine.h>

namespace oxygen::co {

//! An awaiter which immediately reschedules the current task.
/*!
 The Yield awaiter forces the current coroutine to be rescheduled by
 suspending (`await-ready()` returns `false`) and immediately resuming it
 (`await_suspend()` returns the current coroutine handle).

 This can be useful to ensure that any other scheduled tasks get a chance to
 run, or to create a cancellation point in the coroutine.
 */
class Yield {
public:
  // ReSharper disable CppMemberFunctionMayBeStatic
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[nodiscard]] auto await_ready() const noexcept { return false; }

#ifdef __clang__
  // clang-16 manages to optimize away this function entirely
  // in optimized builds.
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  void await_suspend(detail::Handle h) __attribute__((noinline)) { h.resume(); }
#else
  [[nodiscard]] auto await_suspend(detail::Handle const h) { return h; }
#endif

  void await_resume() { }

  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  auto await_cancel([[maybe_unused]] detail::Handle h) noexcept
  {
    return std::true_type {};
  }
  // ReSharper restore CppMemberFunctionMayBeStatic
};

} // namespace oxygen::co

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/OxCo/Detail/CoTag.h>
#include <Oxygen/OxCo/Detail/Promise.h>
#include <Oxygen/OxCo/Detail/TaskAwaiter.h>

namespace oxygen::co {

//! A marker type, primarily shows up as the return-type of a function,
//! indicating that it is an **async function** backed by a c++20 coroutine.
//! \tparam T The type of the value returned by the async function.
/*!
 C++20 introduced support for _coroutines_: functions whose execution can be
 suspended and resumed at various points, which are marked by `co_await` or
 `co_yield` expressions in the body of the function.

 The expression following `co_await` or `co_yield` is often an _awaitable_
 object, and in some cases it can be converted to an _awaitable_ object using
 the `await_transform()` method of the coroutine _promise_. The _awaitable_
 object, can be a _direct awaitable_ when it implements itself the methods
 required in the `Awaiter` concept, or it can be a `co_await` awaitable when it
 has an `operator co_await()` (its own or a global one defined outside its
 class) that returns an _awaiter_.

 The _awaiter_ is the object that interacts directly with the coroutine
 mechanism to control the suspension and resumption of the coroutine. It can be
 extended to handle cancellations, early resumptions, and other coroutine
 control mechanisms.

 Every coroutine has an associated _promise_ object, which is created when the
 coroutine is started. The _promise_ object manages the state of the coroutine
 execution, and is manipulated internally within the coroutine. The _promise_
 object is created by the compiler, and its type is exactly `promise_type`,
 which must be defined within the return type of the coroutine. It is
 responsible for creating the _return object_ object (an instance of Co<T>) of
 the coroutine, manages the result of the coroutine execution (when the
 coroutine returns a result), and can propagate exceptions thrown while it is
 executing.

 == In `oxygen::co` ==

 The `Co<>` marker type is the return type of any _async function_. It is also
 `Awaitable`, which when `co_await`ed, returns `TaskAwaiter`. `TaskAwaiter`
 is the `Awaiter` and the `Awaitable` for the coroutine.

 The `TaskAwaiter` also serves as a task parent for continuation after a
 coroutine completes. It receives the coroutine result (value, exception, or
 cancellation) and can indicate where execution should proceed after the task
 completes.

 The `Promise<T>` class is the promise type for a coroutine that returns `T`. In
 addition to the standard promise methods, it adds extensions for controlling
 cancellation and structured concurrency executions.

 Many helper awaitable types are provided, for representing various typical
 behaviors (ready, suspend forever, suspend always, etc...), wrapping other
 awaitables to add extra functionality (such as cancellation) and for
 implementing useful multiplexers (such as `AllOf` and `AnyOf`) or functional
 algorithms (such as `YieldToRun` and `UntilCancelledAnd`).

 In `oxygen::co`, wrappers and utilities are provided so that awaitables can be
 created from lambdas, functions, other awaitables, or custom-built. The
 simplicity of use also comes with a number of ground rules:

   - Awaitable must be `co_await`ed. This is to avoid memory leaks and proper
     lifetime management. It is not required that the awaitable if `co_await`ed
     by its creator, but it is required that it always has a parent that awaits
     its completion.

   - Objects captured in awaitables must outlive the awaitable. OxCo does a lot
     of things to make it easier to work with lambdas in particular, but it is
     always recommended to run with `Address Sanitizer` while developing.
*/
template <class T = void> class [[nodiscard]] Co : public detail::CoTag {
public:
  using promise_type = detail::Promise<T>;
  using ReturnType = T;

  Co() = default;
  explicit Co(detail::Promise<T>& promise)
    : promise_(&promise)
  {
  }

  [[nodiscard]] auto IsValid() const { return promise_.get() != nullptr; }

  //! This is pretty much the only meaningful operation you can do on a
  //! `Co<T>`. Calling it returns an `Awaitable`, which also acts as the
  //! `Awaiter` for controlling the suspension and resumption of the
  //! coroutine.
  auto operator co_await() noexcept
  {
    return detail::TaskAwaiter<T>(promise_.get());
  }

private:
  auto Release() -> detail::Promise<T>* { return promise_.release(); }

  // Friends for Release()
  friend class Nursery;

  // Hold the promise object for the coroutine ensuring it is not destroyed as
  // long as the coroutine is running.
  detail::PromisePtr<T> promise_;
};

namespace detail {
  //! The promise type for a `Co<T>` object, will always return a `Co<T>`
  //! object from its `get_return_object()` method.
  template <class T> auto Promise<T>::get_return_object() -> Co<T>
  {
    return Co<T>(*this);
  }
} // namespace detail

} // namespace oxygen::co

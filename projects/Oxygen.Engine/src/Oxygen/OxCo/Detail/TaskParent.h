//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/Result.h"

namespace oxygen::co::detail {

class BasePromise;

//! Encapsulates the result of an async task execution and indicates where
//! execution should proceed after the execution completes. Serves as the
//! parent of the task, in the context where "An async task can only run when
//! it's being awaited by another task".
/*!
 In OxCo, `TaskAwaitable` and `Nursery` can serve as the parent of an async
 task, and therefore they derive from this base class. Their states are driven
 by the `Promise` object of the coroutine that they are awaiting.

 This base class implements the parts of `TaskParent` that are independent of
 the return type, and serves as a pure interface to the `TaskParent` in places
 where the return type is not known, or is erased.
 */
class BaseTaskParent {
public:
    //! Called when a task finishes execution (either `StoreValue()` or
    //! `StoreException()` would have been called before).
    /*!
     \returns A coroutine handle to chain execute to (or std::noop_coroutine()).
    */
    virtual auto Continuation(BasePromise*) noexcept -> Handle = 0;

    //! Called when task execution ended with an unhandled exception
    //! (available through `std::current_exception()`).
    virtual void StoreException() = 0;

    //! Called when task confirms its cancellation.
    virtual void Cancelled() { }

protected:
    BaseTaskParent() = default;
    OXYGEN_DEFAULT_COPYABLE(BaseTaskParent)
    OXYGEN_DEFAULT_MOVABLE(BaseTaskParent)

    // Prevent deletion of derived objects through a base pointer.
    /*non-virtual*/ ~BaseTaskParent() = default;
};

//! Represents the parent of an async task that returns an object of type `T`.
//! \tparam T The type of the result of the async task.
/*!
 Implements the result storage part of the `BaseTaskParent` interface and serves
 as an explicitly typed parent for an async task that returns `T`.
*/
template <class T>
class TaskParent : public BaseTaskParent {
public:
    //! Called when task exited normally and returned a value.
    virtual void StoreValue(T t) { result_.StoreValue(std::forward<T>(t)); }

protected:
    TaskParent() = default;
    OXYGEN_DEFAULT_COPYABLE(TaskParent)
    OXYGEN_DEFAULT_MOVABLE(TaskParent)
    ~TaskParent() = default;

    Result<T> result_; // NOLINT(*-non-private-member-variables-in-classes)
};

//! Represents the parent of an async task that returns `void`.
/*!
 Implements the result storage part (when the task completes successfully) of
 the `BaseTaskParent` interface and serves as an explicitly typed parent for an
 async task that returns `void`.
*/
template <>
class TaskParent<void> : public BaseTaskParent {
public:
    //! Called when task exited normally.
    virtual void StoreSuccess() { result_.StoreSuccess(); }

protected:
    TaskParent() = default;
    OXYGEN_DEFAULT_COPYABLE(TaskParent)
    OXYGEN_DEFAULT_MOVABLE(TaskParent)
    ~TaskParent() = default;

    Result<void> result_; // NOLINT(*-non-private-member-variables-in-classes)
};

} // namespace oxygen::co::detail

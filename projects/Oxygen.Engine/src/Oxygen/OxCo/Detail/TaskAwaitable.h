//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Logging.h"
#include "Oxygen/OxCo/Detail/Promise.h"

namespace oxygen::co::detail {

//! Serves as the _awaitable_ and _awaiter_ for an async task. Returned by
//! `Co<T>::operator co_await()`.
//! \tparam T The type of the result produced by the task.
/*!
 This class is the ultimate entry point to manipulate the suspension,
 resumption, cancellation and result of an async task, and can also serve as the
 parent of another async task.
*/
template <class T>
class TaskAwaitable final : public TaskParent<T> {
public:
    //! Creates an instance of `TaskAwaitable` that is not associated with any
    //! promise.
    TaskAwaitable() = default;

    explicit TaskAwaitable(Promise<T>* promise)
        : promise_(promise)
    {
    }

    OXYGEN_DEFAULT_COPYABLE(TaskAwaitable)
    OXYGEN_DEFAULT_MOVABLE(TaskAwaitable)

    //! Destructor.
    /*!
     \note Deletion only allowed through a pointer to `TaskAwaitable`. All base
           classes have a protected or private (with friend) destructor.
    */
    ~TaskAwaitable() = default;

    //! Suspends the calling coroutine, starts the promise and arranges for
    //! proper continuation, after it completes or gets cancelled.
    /*!
     \param h The coroutine handle.
     \return A handle to the coroutine to be resumed. This is not necessarily
     the same as the provided handle `h`.
    */
    [[nodiscard]] auto await_suspend(Handle h)
    {
        DLOG_F(1, "    ...pr {}", fmt::ptr(promise_));
        DCHECK_NOTNULL_F(promise_);
        continuation_ = h;
        return promise_->Start(this, h);
    }

    //! Retrieves the result of the async operation or re-throws an exception
    //! if the task failed or was cancelled.
    /*!
     This method is called when the awaitable resumes execution after the task
     has completed. Calling `Value()` on the result will either return a valid
     result of type `T` or throw an exception if the task failed to execute
     properly or was cancelled.
    */
    auto await_resume() && -> T
    {
        return std::move(this->result_).Value();
    }

    //! Checks if the awaitable is ready with a result now, hence allowing to
    //! completely bypass the costly suspension and resumption of the async
    //! coroutine.
    [[nodiscard]] auto await_ready() const noexcept
    {
        // NOLINTNEXTLINE(*-pro-type-const-cast)
        return promise_->CheckImmediateResult(const_cast<TaskAwaitable*>(this));
    }

    //! Sets the executor for the associated promise.
    /*!

        \param ex Pointer to the executor to set.

        This method sets the executor on the associated promise. This determines
        where the task will be executed.
    */
    void await_set_executor(Executor* ex) noexcept
    {
        DCHECK_NOTNULL_F(promise_);
        promise_->SetExecutor(ex);
    }

    //! Requests cancellation of the operation represented by this awaitable
    //! before `await_suspend()` has been called. This may be called either
    //! before or after `await_ready()`, and regardless of the value returned by
    //! `await_ready()`.
    /*!
        \return By default, we allow a cancellation point before any execution
        of the awaitable. We always returns `false` to indicate that from the
        awaitable point of view, we requested the cancellation, but it is
        important to start the operation and let it decide how to handle the
        cancellation request.

        \see CustomizesEarlyCancel
    */
    [[nodiscard]] auto await_early_cancel() noexcept
    {
        promise_->Cancel();
        return false;
    }

    //!  Requests cancellation of an in-progress operation represented by this
    //!  awaitable.
    /*!
     We always return `false` (a boolean value) to indicate that cancellation is
     in progress and its completion will be signaled by resuming the provided
     handle. However, we do not manage such resumption here.

     By returning `bool` instead of `std::true_type`, we indicate that the
     cancellation might complete asynchronously, and `await_must_resume()` must
     be called to determine whether the cancellation was successful.

     \see Cancellable
    */
    [[nodiscard]] auto await_cancel(Handle /*unused*/) noexcept
    {
        if (promise_) {
            promise_->Cancel();
        } else {
            // If `promise_` is `null`, then `Continuation()` was called, so
            // we're about to be resumed, so the cancel will fail.
        }
        return false;
    }

    //! Hook that will be invoked when the parent task is resumed after a call
    //! to `await_cancel()` or `await_early_cancel()` that does not complete
    //! synchronously, in order to determine whether the resumption indicates
    //! that the operation was cancelled (`false`) or that it completed despite
    //! our cancellation request (`true`).
    /*!
     \return `false` if the operation result indicates cancellation, `true` if
             the operation completed despite the cancellation request and the
             result holds a value or an exception.

     \see CustomizesMustResume
    */
    [[nodiscard]] auto await_must_resume() const noexcept
    {
        return !this->result_.WasCancelled();
    }

private:
    // NB: The `TaskParent` interface is part of the internal machinery of how
    // we manage the state of async tasks, and is not intended to be used
    // directly from client code. Hence, we are intentionally making the
    // implementation of that interface private, communicating that we do not
    // want you to use the interface directly on `TaskAwaitable` objects.

    //! Called when the task completes with an exception. Stores the exception
    //! in the internal `Result<T>` object.
    void StoreException() override
    {
        this->result_.StoreException();
    }

    //! Called when the task is cancelled. Marks the internal `Result<T>` as
    //! cancelled.
    void Cancelled() override
    {
        this->result_.MarkCancelled();
    }

    //! Called when a task finishes execution (either `StoreValue()` or
    //! `StoreException()` would have been called before).
    /*!
     \returns A coroutine handle to chain execute to (or std::noop_coroutine()).

     Resets the promise pointer to `nullptr` to indicate that no further
     operation is expected on this awaitable.
    */
    auto Continuation(BasePromise* /*unused*/) noexcept -> Handle override
    {
        DCHECK_F(this->result_.Completed(), "task exited without `co_return`ing a result");
        promise_ = nullptr;
        return continuation_;
    }

    Promise<T>* promise_ = nullptr;
    Handle continuation_;
};

} // namespace oxygen::co::detail

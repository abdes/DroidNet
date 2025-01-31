//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Logging.h"
#include "Oxygen/OxCo/Detail/MuxHelper.h"
#include "Oxygen/OxCo/Detail/ScopeGuard.h"

namespace oxygen::co::detail {

template <typename T>
concept Multiplexer = requires(T t, std::exception_ptr ex) {
    //! Returns the total number of awaitables being managed.
    { t.Size() } -> std::same_as<size_t>;

    //! Returns the minimum number of awaitables that need to complete before
    //! the multiplexer considers itself ready to resume the parent coroutine.
    { t.MinReady() } -> std::same_as<size_t>;

    //! Attempts to cancel the ongoing awaitables; returns true if all
    //! awaitables were successfully cancelled.
    { t.InternalCancel() } -> std::same_as<bool>;

    //! Returns true if the multiplexer can be skipped (e.g., when all
    //! awaitables can be cancelled without side effects).
    { T::IsSkippable() } -> std::convertible_to<bool>;

    //! Returns true if the multiplexer can be aborted after it has started
    //! (e.g., AnyOf can be abortable, while AllOf might not be).
    { T::IsAbortable() } -> std::convertible_to<bool>;
};

//! A CRTP mixin designed to facilitate the implementation of multiplexing
//! coroutines, which are coroutines that manage multiple awaitables.
/*!
 The MuxBase class provides common functionality needed by such multiplexing
 coroutines, such as handling cancellation, managing coroutine suspension and
 resumption, and exception propagation.
*/
template <class Self>
class MuxBase {
public:
    // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
    MuxBase()
    {
        // Ensure that the derived class is a Multiplexer. Delay the check until
        // now because the derived class was not be fully defined yet.
        static_assert(Multiplexer<Self>);
    }

    //! Handles the early cancellation of the multiplexer before it begins
    //! execution, typically called when the parent coroutine no longer needs
    //! the result.
    /*!
     Attempts to cancel all the awaitables by calling `InternalCancel()` on the
     multiplexer. If the multiplexer is 'skippable', then synchronous
     cancellation is possible, and the multiplexer \b must have cancelled all
     awaitables. Otherwise, the result of `InternalCancel()` indicates whether
     all awaitables were successfully cancelled.

     \return true if all awaitables can be cancelled, false otherwise.
    */
    auto await_early_cancel() noexcept
    {
        bool all_cancelled = self().InternalCancel();
        if constexpr (Self::IsSkippable()) {
            // If all the Awaitables can be skipped, then the whole mux is too.
            DCHECK_F(all_cancelled);
            return std::true_type {};
        } else {
            return all_cancelled;
        }
    }
    //! Handles the cancellation of the multiplexer after it has started
    //! execution, potentially cancelling any pending awaitables. This is
    //! typically invoked when the parent coroutine requests cancellation during
    //! suspension.
    /*!
     Attempts to cancel all the awaitables by calling `InternalCancel()` on the
     multiplexer.

        If the multiplexer is 'abortable', then synchronous cancellation is
        possible, and the multiplexer \b must have cancelled all awaitables.
        Otherwise, the result depends on how many awaitables were cancelled:

           - If all awaitables were successfully cancelled, the method returns
             `true`.

           - If some awaitables were already completed before the cancellation
             occurred, and all remaining ones were successfully cancelled,
             cancellation is good, but does not count a synchronous cancellation
             of the overall mux. The handle `h` is resumed.

           - If cancellation did not complete for some awaitables, the method
             returns `false` and the handle `h` is not resumed.
     */
    auto await_cancel(const Handle h) noexcept
    {
        const bool all_cancelled = [&] {
            // Avoid resuming our parent while we cancel things; we might
            // want to return true instead.
            Handle original_parent = std::exchange(parent_, std::noop_coroutine());
            auto guard = ScopeGuard([this, original_parent]() noexcept { parent_ = original_parent; });
            return self().InternalCancel();
        }();

        if constexpr (Self::IsAbortable()) {
            DCHECK_F(all_cancelled);
            return std::true_type {};
        } else {
            if (all_cancelled) {
                return true;
            }
            if (count_ == self().Size()) {
                // We synchronously cancelled the remaining awaitables, but some
                // had already completed so this doesn't count as a sync-cancel
                // of the overall mux.
                h.resume();
            }
            return false;
        }
    }

    //! Indicates multiplexers that are ready when one awaitable is ready
    //! (AnyOf/OneOf), and useful to determine whether they can be aborted.
    /*!
     If all the Awaitables are Abortable, then the completion of the first one
     will immediately cancel the rest, so if the mux hasn't completed yet, then
     none of the individual awaitables have completed, so await_cancel() of the
     mux will always succeed synchronously too. This is not true for
     AllOf/MostOf, because some of the awaitables might have already completed
     before the cancellation occurs.
    */
    static constexpr bool kDoneOnFirstReady = false;

protected:
    [[nodiscard]] auto self() -> Self& { return *static_cast<Self*>(this); }
    [[nodiscard]] auto self() const -> const Self& { return *static_cast<const Self*>(this); }

    //! Determines whether the coroutine should suspend and stores the parent
    //! coroutine handle for later resumption.
    /*!
     \return `false` if there are no awaitables, `true` otherwise.

     \note Derived classes will typically have custom suspension logic in their
           implementation of `await_suspend()`, but should always call this
           convenience method to determine if they should proceed with their own
           logic.
    */
    [[nodiscard]] auto DoSuspend(const Handle h) -> bool
    {
        if (self().Size() == 0) {
            return false;
        }
        DLOG_F(1, "   ...on Mux<{}/{}> {}", self().MinReady(), self().Size(), fmt::ptr(this));
        parent_ = h;
        return true;
    }

    //! Re-throws any exception that was captured during the execution of the
    //! awaitables.
    /*!
     Multiplexer implementations should call `ReRaise()` in the `await_resume()`
     method to propagate exceptions to the caller. Such exceptions are caught by
     the `MuxHelper` and propagated to the multiplexer when `Invoke()` is
     called.

     \see MuxHelper
     \see Invoke
     */
    void ReRaise() const
    {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    //! Checks whether an exception has been captured.
    [[nodiscard]] auto HasException() const noexcept { return exception_ != nullptr; }

    [[nodiscard]] auto Parent() const noexcept { return parent_; }

private:
    friend Self; // for testing purposes

    //! Called by the underlying awaitables (wrapped in `MuxHelper`) when they
    //! complete, either successfully or with an exception. It tracks the
    //! completion and determines whether to resume the parent coroutine.
    void Invoke(const std::exception_ptr& ex)
    {
        auto i = ++count_;
        bool first_fail = (ex && !exception_);
        if (first_fail) {
            exception_ = ex;
        }
        DLOG_F(1, "Mux<{}/{}> {} invocation {}{}", self().MinReady(),
            self().Size(), fmt::ptr(this), i, (ex ? " with exception" : ""));
        if (i == self().Size()) {
            parent_.resume();
        } else if (first_fail || i == self().MinReady()) {
            // Prevent Double Counting: When we call InternalCancel(), it may
            // cause the remaining awaitables to complete synchronously
            // (immediately) during the cancellation process. These awaitables
            // will call Invoke() recursively, incrementing count_ again. We
            // allow `Invoke` to be re-entrant in this case, while maintaining
            // an accurate count of completed awaitables (not including the one
            // which triggered this invocation).
            --count_;
            self().InternalCancel();
            // After cancelling remaining awaitables, restore the count to
            // include this awaitable.
            if (++count_ == self().Size()) {
                parent_.resume();
            }
        }
    }

    size_t count_ = 0;
    Handle parent_;
    std::exception_ptr exception_;

    template <class, class>
    friend class MuxHelper; // Allow MuxHelper to call Invoke()
};

} // namespace oxygen::co::detail

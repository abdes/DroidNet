//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Detail/Queue.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>
#include <Oxygen/OxCo/EventLoop.h>

namespace oxygen::co {

//! An executor for resuming coroutines and running other actions that might
//! need to be deferred for a short time.
/*!
 The primary reason the executor exists is to enforce the rule, helpful in
 reasoning about cooperative multitasking, that other tasks can only run during
 a `co_await` expression. A synchronous function call such as `event.trigger()`
 might need to wake up tasks that are waiting for the event to occur; those
 tasks can't run immediately, so they get submitted to the executor in order to
 run at the next `co_await` point. More precisely, the executor ensures that
 callbacks submitted to it are not _nested_: something submitted to the executor
 can run only when nothing else that has been submitted is in the middle of
 running.

 The executor runs only on demand, and fully drains its list of pending actions
 every time it runs; it has no concept of scheduling something to run at a time
 other than "as soon as possible". Anything other than a task step, including
 checking for I/O and timeouts, can only run when the executor is idle.

 There is one executor per root of an asynchronous tree. Each executor is
 associated with a particular event loop identity; executors for the same event
 loop will cooperate to ensure that their callbacks are not nested, providing
 similar semantics as if only one executor were used, but executors for
 different event loops are entirely independent.

 The basic pattern used when submitting to the executor is to write
 `executor->RunSoon(<thing>)`. This will run the `<thing>` immediately if there
 is no another executor callback currently running for the same event loop on
 this thread, or else will schedule it to run after whatever is running
 currently.

 \note `executor->capture()` exposes an interface to bypass the executor
 queueing and run a task step synchronously.
*/
class Executor {
    using Task = std::pair<void (*)(void*) noexcept, void*>;
    using Tasks = detail::Queue<Task>;

public:
    struct Capacity {
        static constexpr size_t Default = 128;
        static constexpr size_t Small = 4;
    };

    explicit Executor(const EventLoopID event_loop_id, const size_t capacity = Capacity::Default)
        : event_loop_id_(event_loop_id)
        , buffer_(capacity)
    {
    }

    template <class EventLoopT>
    explicit Executor(EventLoopT&& event_loop, size_t capacity = Capacity::Default)
        : Executor(
              EventLoopTraits<std::decay_t<EventLoopT>>::EventLoopId(std::forward<EventLoopT>(event_loop)),
              capacity)
    {
    }

    ~Executor()
    {
        if (scheduled_ != nullptr) {
            // We had a call pending from another executor. Based on the
            // invariant that we can't be destroyed with our own callbacks
            // scheduled, we must not need that call anymore; the most likely
            // way to hit this is by destroying an UnsafeNursery inside an async
            // task, which deals with the pending callbacks using
            // Executor::drain().
            scheduled_->buffer_.ForEach([&](Task& task) {
                if (task.second == this) {
                    task.first = +[](void*) noexcept { };
                    task.second = nullptr;
                }
            });
        }

        DCHECK_F(buffer_.Empty());

        if (running_ != nullptr) {
            *running_ = false;
        }
        if (Current() == this) {
            Current() = nullptr;
        }
    }

    OXYGEN_MAKE_NON_COPYABLE(Executor)
    OXYGEN_MAKE_NON_MOVABLE(Executor)

    //! Schedules `fn(arg)` to be called on the next executor loop, then runs
    //! the executor loop unless already running.
    template <class T>
    void RunSoon(void (*fn)(T*) noexcept, T* arg) // NOLINT(*-no-recursion)
    {
        Schedule(fn, arg);
        RunSoon();
    }

    template <class T>
    void RunSoon(void (*fn)(const T*) noexcept, const T* arg)
    {
        Schedule(fn, arg);
        RunSoon();
    }

    //! Runs executor loop until it's empty. This function is re-entrant.
    void Drain() { Drain(*ready_); }

    //! Arranges `fn(arg)` to be called on the next executor loop, but does
    //! *not* run the executor loop.
    /*!
     The caller should arrange `RunSoon()` to be called at some point in the
     future, or else the callback will never be called.

     \note We are losing a bit of type safety here because we are storing
           function pointers in the queue with `void *` types but still allowing
           the functions to be defined with real types. In exchange, we're
           keeping the code very simple. Clang-tidy will complain, but we are
           converting from `T*` to `void* and calling the function pointer
           inside `Drain` with `void *`. It works fine as it is and there is no
           simple alternate way to keep the type information without requiring
           RTTI.
    */
    template <class T>
    void Schedule(void (*fn)(T*) noexcept, T* arg)
    {
        // NOLINTNEXTLINE(clang-diagnostic-cast-function-type-strict, *-pro-type-reinterpret-cast)
        ready_->EmplaceBack(reinterpret_cast<void (*)(void*) noexcept>(fn), arg);
    }

    template <class T>
    void Schedule(void (*fn)(const T*) noexcept, const T* arg)
    {
        // NOLINTNEXTLINE(clang-diagnostic-cast-function-type-strict, *-pro-type-reinterpret-cast, *-pro-type-const-cast)
        ready_->EmplaceBack(reinterpret_cast<void (*)(void*) noexcept>(fn), const_cast<T*>(arg));
    }

    //! Runs the executor loop.
    /*!
     If called from within the loop, does nothing; if called from another
     executor's loop, schedules this executor's event loop to be run from
     current executor (therefore not introducing an interruption point).

     \note This will continue running executor until its run queue empties, and
           only then would return, so I/O checks can be made.
    */
    void RunSoon() noexcept // NOLINT(*-no-recursion)
    {
        if (running_ != nullptr || scheduled_ != nullptr) {
            // Do nothing; our callbacks are already slated to run soon
        } else if (Current() != nullptr && Current()->event_loop_id_ == event_loop_id_) {
            // Schedule the current executor to run our callbacks
            scheduled_ = Current();
            scheduled_->RunSoon(
                +[](Executor* ex) noexcept { ex->RunOnce(); }, this);
        } else {
            // No current executor, or it's for a different event loop:
            // run our callbacks immediately
            RunOnce();
        }
    }

    //! Runs the function, temporarily capturing any tasks scheduled into a
    //! separate list. Then runs everything in the list.
    /*!
     \note Tasks scheduled as a result of executing the capture list go into
           previously used list.
    */
    template <class Fn>
    void Capture(Fn fn, const size_t capacity = Capacity::Small)
    {
        Tasks tmp { capacity };
        detail::ScopeGuard guard([&]() noexcept { Drain(tmp); });

        Tasks* old_ready = ready_;
        // ReSharper disable once CppDFALocalValueEscapesFunction
        ready_ = &tmp;
        detail::ScopeGuard guard2([&old_ready, this]() noexcept {
            ready_ = old_ready;
        });

        fn();
    }

private:
    void RunOnce() noexcept
    {
        scheduled_ = nullptr;
        if (running_ == nullptr) {
#if !defined(NDEBUG)
            LOG_SCOPE_F(1, "Executor running");
            DLOG_F(1, "Event loop ID: {}", fmt::ptr(event_loop_id_.Get()));
#endif
            Drain();
        }
    }

    void Drain(Tasks& tasks) noexcept
    {
        Executor* prev = Current();
        Current() = this;
        detail::ScopeGuard guard([&]() noexcept { Current() = prev; });

        bool b = true;
        if (running_ == nullptr) {
            // `running_` will be reset before this method returns, via the scope
            // guard.
            // ReSharper disable once CppDFALocalValueEscapesFunction
            running_ = &b;
        }
        const bool* running = running_;
        detail::ScopeGuard guard2([&]() noexcept {
            if (*running && running_ == &b) {
                running_ = nullptr;
            }
        });

        while (*running && !tasks.Empty()) {
            auto [fn, arg] = tasks.Front();
            tasks.PopFront();
            fn(arg);
        }
    }

    static auto Current() noexcept -> Executor*&
    {
        // NB: Executor::Current() is the executor that is currently running
        // (i.e., is inside a call to Drain()), and will be nullptr if all tasks
        // are blocked waiting for something.
        thread_local Executor* executor = nullptr;
        return executor;
    }

    EventLoopID event_loop_id_;
    Tasks buffer_;

    //! Currently used list of ready tasks. Normally points to `buffer_`,
    //! but can be temporarily replaced from `Capture()`.
    Tasks* ready_ = &buffer_;

    bool* running_ { nullptr };

    //! Stores the pointer to an outer executor on which we've scheduled our
    //! runOnce() to run soon, or nullptr if not scheduled.
    Executor* scheduled_ = nullptr;
};

namespace detail {
    class GetExecutor {
        Executor* executor_ = nullptr;

    public:
        //! @{
        //! Implementation of the awaiter interface.
        // ReSharper disable CppMemberFunctionMayBeStatic
        // NOLINTBEGIN(*-convert-member-functions-to-static, *-use-nodiscard)

        void await_set_executor(Executor* executor) noexcept
        {
            executor_ = executor;
        }
        [[nodiscard]] auto await_ready() const noexcept -> bool { return executor_ != nullptr; }
        auto await_suspend(const Handle h) -> Handle { return h; }
        [[nodiscard]] auto await_resume() const -> Executor* { return executor_; }

        // ReSharper disable CppMemberFunctionMayBeStatic
        // NOLINTEND(*-convert-member-functions-to-static, *-use-nodiscard)
        //! @}
    };
} // namespace detail

/// Returns an awaitable that you can use in an async context to get the
/// currently-running executor.
inline auto GetExecutor() -> Awaitable<Executor*> auto
{
    return detail::GetExecutor {};
}

} // namespace oxygen::co

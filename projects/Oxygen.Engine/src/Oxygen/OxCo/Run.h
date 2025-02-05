//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/NoInline.h"
#include "Oxygen/OxCo/Detail/AwaitableAdapter.h"
#include "Oxygen/OxCo/Detail/GetAwaitable.h"
#include "Oxygen/OxCo/Detail/TaskFrame.h"
#include "Oxygen/OxCo/EventLoop.h"
#include "Oxygen/OxCo/Executor.h"

namespace oxygen::co {

inline auto CurrentRunExecutor() -> Executor*; // defined below

namespace detail {

    class RunnerTracking {
        template <class EventLoop>
        friend class Runner;
        friend auto co::CurrentRunExecutor() -> Executor*;

        static auto CurrentExecutor() -> Executor*&
        {
            thread_local Executor* executor = nullptr;
            return executor;
        }
    };

    template <class EventLoop>
    class Runner : /*private*/ RunnerTracking, /*private*/ TaskFrame {
    public:
        explicit Runner(EventLoop& loop)
            : event_loop_(&loop)
        {
        }

        template <class Awaitable>
        OXYGEN_NOINLINE auto Run(Awaitable&& awaitable) && -> decltype(auto)
        {
            AwaitableAdapter<Awaitable&&> adapter(
                std::forward<Awaitable>(awaitable));
            using Traits = EventLoopTraits<std::decay_t<EventLoop>>;
            DCHECK_NOTNULL_F(event_loop_);
            if constexpr (requires { Traits::IsRunning(*event_loop_); }) {
                DCHECK_F(!Traits::IsRunning(*event_loop_));
            }

            // The executor is created and needs to be valid only for the
            // duration of Run()
            Executor executor(*event_loop_);
            // ReSharper disable once CppDFALocalValueEscapesFunction
            executor_ = &executor;
            Executor* prev_exec = std::exchange(CurrentExecutor(), &executor);
            ScopeGuard guard([this, prev_exec]() noexcept {
                executor_ = nullptr;
                CurrentExecutor() = prev_exec;
            });

            if (!adapter.await_ready()) {
                resume_fn = +[](CoroutineFrame* frame) {
                    auto runner = static_cast<Runner*>(frame);
                    runner->executor_->RunSoon(
                        +[](Runner* r) noexcept {
                            EventLoop* loop = std::exchange(r->event_loop_, nullptr);
                            Traits::Stop(*loop);
                        },
                        runner);
                };
                // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                ProgramCounter(reinterpret_cast<uintptr_t>(OXYGEN_RETURN_ADDRESS()));
                adapter.await_set_executor(&executor);
                adapter.await_suspend(ToHandle()).resume();
                executor.RunSoon();
                // ReSharper disable CppDFAConstantConditions
                // the event_loop_ is reset in the `resume_fn`
                if (event_loop_) {
                    Traits::Run(*event_loop_);
                }
                if (event_loop_) {
                    // The event loop was stopped before the awaitable completed.
                    // Run the executor one more time in case that's enough.
                    executor.Drain();
                }
                if (event_loop_) [[unlikely]] {
                    // Nope, still running, so it must be waiting on some
                    // I/O that's not going to happen anymore. Fail.
                    // Do our best to clean up if there is a custom implementation
                    // of FAIL_FOR_DANGLING_TASKS that throws an exception.
                    ScopeGuard cleanup_guard([&]() noexcept {
                        if (adapter.await_cancel(ToHandle())) {
                            // cancelled immediately
                            return;
                        }
                        executor.Drain();
                        if (!event_loop_) {
                            // cancelled after a spin of the executor
                            return;
                        }
                        // failed to cancel -- we already know something is wrong,
                        // avoid follow-on errors that obscure the original issue
                        adapter.Abandon();
                    });
                    // We don't have anything to return below, so if the
                    // above failure allowed execution to proceed, we must:
                    ABORT_F("Event loop stopped before the awaitable passed to "
                            "oxygen::co::Run() completed");
                }
                // ReSharper restore CppDFAConstantConditions
            }

            return adapter.await_resume();
        }

    private:
        EventLoop* event_loop_;
        Executor* executor_ = nullptr;
    };

    //! Run a task or other awaitable from non-async context, using the given
    //! event loop (which must not already be running). This is the entry point
    //! into the library for your application.
    template <class EventLoop, class Awaitable>
    auto Run(EventLoop& loop, Awaitable&& awaitable) -> decltype(auto)
    {
        return detail::Runner(loop)
            .Run(detail::GetAwaitable(std::forward<Awaitable>(awaitable)));
    }

} // namespace detail

//! Run a task or other awaitable from non-async context, using the given event
//! loop (which must not already be running). This is the entry point into the
//! library for your application.
template <class EventLoop, class Awaitable>
OXYGEN_NOINLINE auto Run(EventLoop& event_loop, Awaitable&& awaitable) -> decltype(auto)
{
    return detail::Runner(event_loop).Run(detail::GetAwaitable(std::forward<Awaitable>(awaitable)));
}

/// Returns a pointer to the executor associated with the current `co::Run()`.
/// Example use case: collecting the task tree from non-async context (signal
/// handler, etc.).
inline auto CurrentRunExecutor() -> Executor*
{
    return detail::RunnerTracking::CurrentExecutor();
}

} // namespace oxygen::co

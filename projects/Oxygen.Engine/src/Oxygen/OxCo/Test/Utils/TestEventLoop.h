//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidConstOrRefDataMembers
#pragma once

#include <chrono>
#include <functional>
#include <map>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/ScopeGuard.h"
#include "Oxygen/OxCo/EventLoop.h"

using oxygen::co::detail::ScopeGuard;
using std::chrono::milliseconds;

namespace oxygen::co::testing {

struct NonCancellableTag { };
constexpr NonCancellableTag kNonCancellable;

using EventQueue = std::multimap<milliseconds, std::function<void()>>;

class TestEventLoop {
public:
    //
    // Public EventLoop-like interface
    //
    void Run()
    {
        DLOG_F(INFO, "=== running test case ===");
        running_ = true;
        // ReSharper disable once CppDFAConstantConditions (stopped from outside)
        while (running_) {
            // If this assertion fires, your test would have
            // deadlocked: there are tasks waiting for something, but
            // nothing more will happen.
            CHECK_F(!events_.empty());
            Step();
        }
    }
    TestEventLoop() = default;
    ~TestEventLoop()
    {
        if (!events_.empty()) {
            DLOG_F(INFO, "=== running event leftovers ===");
            while (!events_.empty()) {
                Step();
            }
        }
        DLOG_F(INFO, "=== done ===");
    }

    OXYGEN_MAKE_NON_COPYABLE(TestEventLoop)
    OXYGEN_MAKE_NON_MOVEABLE(TestEventLoop)

    void Stop() { running_ = false; }
    [[nodiscard]] auto IsRunning() const { return running_; }

    //
    // Fixture interface visible to test bodies
    //

    [[nodiscard]] auto Now() const { return now_; }

    void Schedule(const milliseconds delay, std::function<void()> cb)
    {
        events_.emplace(now_ + delay, std::move(cb));
    }

    template <bool Cancellable>
    class SleepAwaitable {
    public:
        SleepAwaitable(TestEventLoop& event_loop, const milliseconds delay)
            : event_loop_(event_loop)
            , delay_(delay)
        {
        }

        ~SleepAwaitable() { CHECK_F(!suspended_); }

        OXYGEN_MAKE_NON_COPYABLE(SleepAwaitable)
        OXYGEN_DEFAULT_MOVABLE(SleepAwaitable)

        // ReSharper disable CppMemberFunctionMayBeStatic
        auto await_early_cancel() noexcept
        {
            if constexpr (Cancellable) {
                DLOG_F(1, "Sleep {} ({} ms) early cancellable", fmt::ptr(this),
                    DelayMilliseconds());
                return std::true_type {};
            } else {
                DLOG_F(1, "sleep {} ({} ms) NOT early cancellable", fmt::ptr(this),
                    DelayMilliseconds());
                return false;
            }
        }

        auto await_ready() const noexcept -> bool { return false; }
        void await_suspend(const detail::Handle h)
        {
            DLOG_F(1, "    ...on sleep {} ({} ms)", fmt::ptr(this), DelayMilliseconds());
            suspended_ = true;
            parent_ = h;
            it_ = event_loop_.events_.emplace(event_loop_.now_ + delay_, [this] {
                DLOG_F(1, "sleep {} ({} ms) resuming parent", fmt::ptr(this),
                    DelayMilliseconds());
                suspended_ = false;
                parent_.resume();
            });
        }
        auto await_cancel(detail::Handle) noexcept
        {
            if constexpr (Cancellable) {
                LOG_F(1, "Sleep {} ({} ms) cancelling", fmt::ptr(this), DelayMilliseconds());
                event_loop_.events_.erase(it_);
                suspended_ = false;
                return std::true_type {};
            } else {
                DLOG_F(1, "Sleep {} ({} ms) reject cancellation", fmt::ptr(this),
                    DelayMilliseconds());
                return false;
            }
        }
        [[nodiscard]] auto await_must_resume() const noexcept
        {
            // shouldn't actually be called unless await_cancel() returns false
            CHECK_F(!Cancellable);
            if constexpr (Cancellable) {
                return std::false_type {};
            } else {
                return true;
            }
        }
        void await_resume() { suspended_ = false; }
        // ReSharper restore CppMemberFunctionMayBeStatic

    private:
        [[nodiscard]] auto DelayMilliseconds() const -> long { return static_cast<long>(delay_.count()); }

        TestEventLoop& event_loop_;
        // ReSharper disable CppDFANotInitializedField (initialized by SleepAwaitable)
        milliseconds delay_;
        detail::Handle parent_;
        EventQueue::iterator it_;
        bool suspended_ = false;
    };

    auto Sleep(const milliseconds tm) -> Awaitable<void> auto
    {
        return SleepAwaitable<true>(*this, tm);
    }

    auto Sleep(const milliseconds tm, NonCancellableTag) -> Awaitable<void> auto
    {
        return SleepAwaitable<false>(*this, tm);
    }

private:
    void Step()
    {
        CHECK_F(!events_.empty());
        auto [time, func] = *events_.begin();

        if (now_ != time) {
            now_ = time;
            DLOG_F(1, "-- {} ms --",
                std::chrono::duration_cast<std::chrono::milliseconds>(now_)
                    .count());
        }
        events_.erase(events_.begin());
        func();
    }

    EventQueue events_ {};
    bool running_ { false };
    milliseconds now_ = milliseconds(0);
};

} // namespace oxygen::co::testing

template <>
struct oxygen::co::EventLoopTraits<oxygen::co::testing::TestEventLoop> {
    static auto EventLoopId(testing::TestEventLoop& p) noexcept -> EventLoopID
    {
        return EventLoopID(&p);
    }
    static void Run(testing::TestEventLoop& p) { p.Run(); }
    static void Stop(testing::TestEventLoop& p) noexcept { p.Stop(); }
    static auto IsRunning(const testing::TestEventLoop& p) noexcept -> bool { return p.IsRunning(); }
};

static_assert(oxygen::co::detail::Cancellable<oxygen::co::testing::TestEventLoop::SleepAwaitable<true>>);

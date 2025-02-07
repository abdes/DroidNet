//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Algorithms.h"
#include "Awaitables.h"
#include "ParkingLot.h"

#include "Oxygen/OxCo/Nursery.h"
#include "Platform.h"
#include <Oxygen/OxCo/Detail/ParkingLotImpl.h>
#include <Oxygen/OxCo/Shared.h>

using namespace std::chrono_literals;

using namespace oxygen::co;
using namespace oxygen::co::detail;

template <typename T>
class EventStream {
public:
    class Sink {
    public:
        explicit Sink(EventStream& object)
            : object_(object)
        {
        }

        [[nodiscard]] auto await_ready() const noexcept { return data_.has_value(); }
        void await_suspend(const Handle h)
        {
            handle_ = h;
            object_.sink_ = this;
        }
        auto await_resume() && -> T
        {
            object_.sink_ = nullptr;
            DCHECK_F(data_.has_value());
            return std::exchange(data_, std::nullopt).value(); // NOLINT(bugprone-unchecked-optional-access)
        }
        auto await_cancel(Handle) noexcept
        {
            object_.sink_ = nullptr;
            handle_ = std::noop_coroutine();
            return std::true_type {};
        }

        friend class EventStream;

    private:
        void ProcessEvent(T&& value) // NOLINT(*-rvalue-reference-param-not-moved)
        {
            DCHECK_F(!data_.has_value());
            data_.emplace(std::forward<T>(value));
            std::exchange(handle_, std::noop_coroutine()).resume();
        }

        EventStream& object_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        Handle handle_;
        std::optional<T> data_ {};
    };

    //! Returns an `Awaitable` which, when `co_await`'ed, suspends the caller
    //! until any an event is made available through a call to `ProcessEvent()`.
    [[nodiscard]] auto WaitForEvent()
    {
        if (sink_) {
            LOG_F(ERROR, "Illegal attempt to co_await on an `EventStream` which already has a sink.");
            throw std::runtime_error("EventStream already has a sink");
        }
        return Sink(*this);
    }

    //! Resume the waiter, waiting on the `EventStream` sink, and provide it
    //! with the event value.
    void ProcessEvent(T&& value)
    {
        if (!Ready()) {
            LOG_F(ERROR, "Illegal attempt to call `ProcessEvent()` on an `EventStream`, which is not ready.");
            throw std::runtime_error("EventStream sink is not ready to process a new event");
        }

        DCHECK_NOTNULL_F(sink_);
        sink_->ProcessEvent(std::forward<T>(value));
    }

    //! Returns true if the `EventStream` can accept events for processing by its sink.
    [[nodiscard]] auto Ready() const { return sink_ != nullptr && !sink_->data_.has_value(); }

private:
    Sink* sink_ {};
};

// class PlatformInput : public ParkingLot {
// public:
//     int event;
// };

class AsyncEngine {
public:
    AsyncEngine(std::shared_ptr<oxygen::Platform> platform)
        : platform_(platform)
    {
    }

    void RunEventLoop()
    {
        // bool suspend = false;
        // auto suspend_handler = [&suspend](asio::error_code ec) {
        //     if (ec) {
        //         DLOG_F(ERROR, "error {}", ec.message());
        //         return;
        //     }
        //     suspend = false;
        // };
        // asio::steady_timer timer(io);

        // int event = 1;
        while (running_) {
            platform_->Async().PollOne();
            platform_->Events().PollOne();
            // auto run = platform_->Input().PollOne();
            // if (run > 0) {
            //     auto& event = platform_->Input().Event().value();
            //     DLOG_F(INFO, "event: {}", *event.NativeEventAs<int>());
            // }

            // if (suspend)
            //     continue;

            // if (input_system_.Ready()) {
            //     DLOG_F(INFO, "queued event {}", event);
            //     input_system_.ProcessEvent(event++);
            // }

            // if (auto sent = stream.trySend(++event); sent != 1) {
            //     DLOG_F(INFO, "failed to send event {}", event);
            //     suspend = true;
            //     timer.expires_after(1s);
            //     timer.async_wait(suspend_handler);
            // } else {
            //     DLOG_F(INFO, "sent event {}", event);
            // }
        }
    }

    auto Start(TaskStarted<> started = {}) -> Co<>
    {
        return OpenNursery(nursery_, std::move(started));
    }

    void Run()
    {
        auto input_system = [&]() -> Co<> {
            auto& input = platform_->Input();
            auto count = 0;
            while (running_) {
                auto event = co_await input;
                ++count;
                LOG_F(INFO, " + input system: {}", event);
                if (count == 1) {
                    co_await platform_->Async().SleepFor(500ms);
                }
            }
        };
        auto imgui = [&]() -> Co<> {
            auto& input = platform_->Events();
            while (running_) {
                auto& event = co_await input.WaitForNextEvent();
                auto native_event = *event.NativeEventAs<int>();
                LOG_F(INFO, " + imgui: {}", native_event);
                if (native_event % 2 == 0) {
                    event.SetHandled();
                }
                // co_await platform_->Async().SleepFor( 100ms);
            }
        };

        running_ = true;

        // nursery_->Start(&AsyncEngine::Clock, this);
        DLOG_F(INFO, "Imgui task");
        nursery_->Start(imgui);
        DLOG_F(INFO, "Input system task");
        nursery_->Start(input_system);
    }

    void Stop()
    {
        running_ = false;
    }

    [[nodiscard]] auto IsRunning() const
    {
        return running_;
    }

private:
    auto Clock() const -> Co<>
    {
        int tick { 0 };
        std::chrono::nanoseconds delta { 1s };
        while (running_) {
            auto start = std::chrono::high_resolution_clock::now();
            co_await platform_->Async().SleepFor(1s);
            auto expired_at = std::chrono::high_resolution_clock::now();
            auto delay = expired_at - start;
            delta = 1s - (expired_at - start) + 1s;
            LOG_F(INFO, "tick {}s - delay: {} - delta {}", ++tick, delay.count(), delta.count());
        }
    }

    bool running_ = false;
    Nursery* nursery_ {};

    std::shared_ptr<oxygen::Platform> platform_;

    // asio::io_context io;
    // EventStream<int> stream { 3 };
    // EventStream<int> input_system_;
};

template <>
struct oxygen::co::EventLoopTraits<AsyncEngine> {
    static void Run(AsyncEngine& engine) { engine.RunEventLoop(); }
    static void Stop(AsyncEngine& engine) { engine.Stop(); }
    static auto IsRunning(const AsyncEngine& engine) -> bool { return engine.IsRunning(); }
    static auto EventLoopId(const AsyncEngine& engine) -> EventLoopID { return EventLoopID(&engine); }
};

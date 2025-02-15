//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <random>
#include <utility>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/BroadcastChannel.h"
#include "Oxygen/OxCo/Channel.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Event.h"
#include "Oxygen/OxCo/Nursery.h"
#include "Oxygen/OxCo/ParkingLot.h"
#include "Oxygen/OxCo/Run.h"
#include "Oxygen/OxCo/Semaphore.h"
#include "Oxygen/OxCo/Shared.h"
#include "Utils/TestEventLoop.h"

#include <iostream>

using std::chrono::milliseconds;
using namespace std::chrono_literals;

using oxygen::co::BroadcastChannel;
using oxygen::co::Channel;
using oxygen::co::Co;
using oxygen::co::Event;
using oxygen::co::kJoin;
using oxygen::co::kYield;
using oxygen::co::Nursery;
using oxygen::co::ParkingLot;
using oxygen::co::Run;
using oxygen::co::Semaphore;
using oxygen::co::Shared;
using oxygen::co::TaskStarted;
using oxygen::co::testing::TestEventLoop;

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)
// NOLINTBEGIN(*-avoid-do-while)

namespace {

using EventType = std::string;

auto MakeEvent()
{
    return "Hello World!";
}

void DoSomething(EventType event)
{
    for (int i = 0; i < 10; ++i) {
        std::ranges::reverse(event);
    }
}

TEST_CASE("DoSomething timing")
{
    BENCHMARK("DoSomething")
    {
        DoSomething(MakeEvent());
    };
}

constexpr size_t kIterations { 1'000'000 };

TEST_CASE("Event Stream - Synchronized Shared Benchmark")
{
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;

    TestEventLoop el;

    class SharedEventSource {
    public:
        void PollOne() { poll_.UnParkAll(); }

        auto NextEvent()
        {
            MaybeBootstrap();
            return NextSlot().Awaitable();
        }
        auto Lock() { return CurrentSlot().Lock(); }

    private:
        void MaybeBootstrap()
        {
            static bool bootstrapped { false };

            if (bootstrapped) {
                return;
            }
            current_slot_index_ = 0;
            NextSlot().Initialize(this);
            bootstrapped = true;
        }

        auto PumpEvent() -> Co<EventType>
        {
            co_await AllOf(
                poll_.Park(),
                [this]() -> Co<> {
                    auto _ = co_await CurrentSlot().Lock();
                });
            auto event = MakeEvent();

            // Prepare the next slot
            current_slot_index_ ^= 1;
            NextSlot().Initialize(this);

            // Return the pumped event
            co_return event;
        }

        struct Slot {
            using AsyncEventPumper = std::function<Co<EventType>()>;

            Semaphore ready { 1 };
            Shared<AsyncEventPumper> event_awaitable;

            void Initialize(SharedEventSource* pump)
            {
                event_awaitable.~Shared();
                new (&event_awaitable) Shared<AsyncEventPumper>(std::in_place,
                    [pump]() -> Co<EventType> {
                        co_return co_await pump->PumpEvent();
                    });
            }

            auto Lock() -> Semaphore::Awaiter<Semaphore::LockGuard>
            {
                return ready.Lock();
            }

            auto Awaitable() -> Shared<AsyncEventPumper>&
            {
                return event_awaitable;
            }
        };

        auto CurrentSlot() -> Slot& { return event_slots_[current_slot_index_]; }
        auto NextSlot() -> Slot& { return event_slots_[current_slot_index_ ^ 1]; }

        std::array<Slot, 2> event_slots_ {};
        uint8_t current_slot_index_ { 0 };

        ParkingLot poll_ {};
    };

    SharedEventSource sh_pump;
    size_t events_processed { 0UL };
    bool done { false };

    BENCHMARK("synchronized shared")
    {
        Run(el, [&]() -> Co<> {
            OXCO_WITH_NURSERY(nursery)
            {
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    while (!done) {
                        sh_pump.PollOne();
                        // co_await el.Sleep(1ms);
                        co_await kYield;
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        const auto event = co_await sh_pump.NextEvent();
                        auto _ = sh_pump.Lock();
                        ++events_processed;
                        CHECK_EQ_F(event, MakeEvent());
                        DoSomething(event);
                        done = events_processed == kIterations;
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        const auto event = co_await sh_pump.NextEvent();
                        auto _ = sh_pump.Lock();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        const auto event = co_await sh_pump.NextEvent();
                        auto _ = sh_pump.Lock();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        const auto event = co_await sh_pump.NextEvent();
                        auto _ = sh_pump.Lock();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                co_return kJoin;
            };
        });
    };
}

TEST_CASE("Event Stream - Multi Channel benchmark")
{
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;

    TestEventLoop el;

    class MultiChannelEventSource {
    public:
        void PollOne() { poll_.UnParkAll(); }

        std::array<Channel<EventType>, 4> channels_;

        auto PumpEvent() -> Co<>
        {
            co_await poll_.Park();
            auto event = MakeEvent();

            // Write the pumped event to all channels
            for (auto& chan : channels_) {
                co_await chan.Send(event);
            }
        }

    private:
        ParkingLot poll_ {};
    };

    MultiChannelEventSource mc_pump;
    size_t events_processed { 0UL };
    bool done { false };

    BENCHMARK("multi-channel")
    {
        return Run(el, [&]() -> Co<> {
            OXCO_WITH_NURSERY(nursery)
            {
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    while (!done) {
                        mc_pump.PollOne();
                        // co_await el.Sleep(1ms);
                        co_await kYield;
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    while (!done) {
                        co_await mc_pump.PumpEvent();
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        auto event = co_await mc_pump.channels_[0].Receive();
                        ++events_processed;
                        CHECK_EQ_F(event, MakeEvent());
                        // Do something that takes a long time with the event
                        DoSomething(*event);
                        done = events_processed == kIterations;
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        const auto event = co_await mc_pump.channels_[1].Receive();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        const auto event = co_await mc_pump.channels_[2].Receive();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    started();
                    while (!done) {
                        const auto event = co_await mc_pump.channels_[3].Receive();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                co_return kJoin;
            };
        });
    };
}

TEST_CASE("Event Stream - BroadcastChannel benchmark")
{
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;

    TestEventLoop el;

    class BroadcastChannelEventSource {
    public:
        void PollOne() { poll_.UnParkAll(); }

        oxygen::co::BroadcastChannel<EventType> channel_;

        auto PumpEvent() -> Co<>
        {
            co_await poll_.Park();
            auto event = MakeEvent();

            // Write the pumped event to all channels
            auto& w = channel_.ForWrite();
            co_await w.Send(event);
        }

    private:
        ParkingLot poll_ {};
    };

    BroadcastChannelEventSource bc_pump;
    size_t events_processed { 0UL };
    bool done { false };

    BENCHMARK("broadcast-channel")
    {
        return Run(el, [&]() -> Co<> {
            OXCO_WITH_NURSERY(nursery)
            {
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    while (!done) {
                        bc_pump.PollOne();
                        // co_await el.Sleep(1ms);
                        co_await kYield;
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    while (!done) {
                        co_await bc_pump.PumpEvent();
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    auto r = bc_pump.channel_.ForRead();
                    started();
                    while (!done) {
                        auto event = co_await r.Receive();
                        ++events_processed;
                        CHECK_EQ_F(event, MakeEvent());
                        // Do something that takes a long time with the event
                        DoSomething(*event);
                        done = events_processed == kIterations;
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    auto r = bc_pump.channel_.ForRead();
                    started();
                    while (!done) {
                        const auto event = co_await r.Receive();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    auto r = bc_pump.channel_.ForRead();
                    started();
                    while (!done) {
                        const auto event = co_await r.Receive();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                nursery.Start([&](TaskStarted<> started = {}) -> Co<> {
                    auto r = bc_pump.channel_.ForRead();
                    started();
                    while (!done) {
                        const auto event = co_await r.Receive();
                        CHECK_EQ_F(event, MakeEvent());
                    }
                });
                co_return kJoin;
            };
        });
    };
}

} // namespace

// NOLINTEND(*-avoid-do-while)
// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-reference-coroutine-parameters)

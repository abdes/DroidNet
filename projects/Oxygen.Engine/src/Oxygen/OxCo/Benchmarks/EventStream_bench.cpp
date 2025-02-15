//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <random>

#include <benchmark/benchmark.h>

#include "../Test/Utils/TestEventLoop.h"
#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/BroadcastChannel.h"
#include "Oxygen/OxCo/Channel.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Event.h"
#include "Oxygen/OxCo/Nursery.h"
#include "Oxygen/OxCo/ParkingLot.h"
#include "Oxygen/OxCo/RepeatableShared.h"
#include "Oxygen/OxCo/Run.h"
#include "Oxygen/OxCo/Semaphore.h"
#include "Oxygen/OxCo/Shared.h"

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

namespace {

using EventType = std::string;
constexpr size_t kIterations { 10 };

auto MakeEvent() -> EventType
{
    return std::string(40, 'A'); // Generate a string with 256 'A' characters
}

class SharedEventSource {
public:
    SharedEventSource()
        : repeatable_shared_([this]() -> Co<EventType> { co_return co_await PumpEvent(); })
    {
    }

    void PollOne() { poll_.UnParkAll(); }

    auto NextEvent()
    {
        return repeatable_shared_.Next();
    }

    auto Lock() { return repeatable_shared_.Lock(); }

private:
    auto PumpEvent() -> Co<EventType>
    {
        co_await poll_.Park();
        auto event = MakeEvent();
        co_return event;
    }

    oxygen::co::RepeatableShared<EventType> repeatable_shared_;
    ParkingLot poll_;
};

class MultiChannelEventSource {
public:
    void PollOne() { poll_.UnParkAll(); }

    std::array<Channel<EventType>, 8> channels_;

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

class BroadcastChannelEventSource {
public:
    BroadcastChannelEventSource()
        : channel_()
    {
    }

    void PollOne() { poll_.UnParkAll(); }

    BroadcastChannel<EventType> channel_;

    auto PumpEvent() -> Co<>
    {
        co_await poll_.Park();
        // Write the pumped event to all channels
        auto& w = channel_.ForWrite();
        auto event = MakeEvent();
        co_await w.Send(event);
    }

private:
    ParkingLot poll_ {};
};

void DoSetup(const benchmark::State& /*state*/)
{
    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
}

void BM_SharedEventSource(benchmark::State& state)
{
    TestEventLoop el;
    SharedEventSource sh_pump;
    size_t events_processed { 0UL };
    bool done { false };

    for (auto _ : state) {
        state.PauseTiming();
        events_processed = 0;
        done = false;
        state.ResumeTiming();

        Run(el, [&]() -> Co<> {
            OXCO_WITH_NURSERY(nursery)
            {
                nursery.Start([&]() -> Co<> {
                    while (!done) {
                        sh_pump.PollOne();
                        co_await kYield;
                    }
                });

                for (int i = 0; i < 8; ++i) {
                    nursery.Start([&, is_counter = i == 0]() -> Co<> {
                        co_await kYield;
                        while (!done) {
                            auto &event = co_await sh_pump.NextEvent();
                            auto lock = co_await sh_pump.Lock();
                            if (is_counter) {
                                ++events_processed;
                                done = events_processed == kIterations;
                            }
                            benchmark::DoNotOptimize(event);
                        }
                    });
                }
                co_return kJoin;
            };
        });
    }
    state.SetItemsProcessed(state.iterations() * kIterations);
}
BENCHMARK(BM_SharedEventSource)->Setup(DoSetup)->Repetitions(5)->ReportAggregatesOnly(true);

void BM_MultiChannel(benchmark::State& state)
{
    TestEventLoop el;
    MultiChannelEventSource mc_pump;
    size_t events_processed { 0UL };
    bool done { false };

    for (auto _ : state) {
        state.PauseTiming();
        events_processed = 0;
        done = false;
        state.ResumeTiming();

        Run(el, [&]() -> Co<> {
            OXCO_WITH_NURSERY(nursery)
            {
                nursery.Start([&]() -> Co<> {
                    while (!done) {
                        mc_pump.PollOne();
                        co_await kYield;
                    }
                });

                nursery.Start([&]() -> Co<> {
                    while (!done) {
                        co_await mc_pump.PumpEvent();
                    }
                });

                // Reader tasks
                for (size_t i = 0; i < mc_pump.channels_.size(); ++i) {
                    nursery.Start([&, i, is_counter = i == 0]() -> Co<> {
                        while (!done) {
                            auto event = co_await mc_pump.channels_[i].Receive();
                            if (is_counter) {
                                ++events_processed;
                                done = events_processed == kIterations;
                            }
                            benchmark::DoNotOptimize(event);
                        }
                    });
                }
                co_return kJoin;
            };
        });
    }
    state.SetItemsProcessed(state.iterations() * kIterations);
}
BENCHMARK(BM_MultiChannel)->Setup(DoSetup)->Repetitions(5)->ReportAggregatesOnly(true);

void BM_BroadcastChannel(benchmark::State& state)
{
    TestEventLoop el;
    BroadcastChannelEventSource bc_pump;
    size_t events_processed { 0UL };
    bool done { false };

    for (auto _ : state) {
        state.PauseTiming();
        events_processed = 0;
        done = false;
        state.ResumeTiming();

        Run(el, [&]() -> Co<> {
            OXCO_WITH_NURSERY(nursery)
            {
                // Poller and pump tasks
                nursery.Start([&]() -> Co<> {
                    while (!done) {
                        bc_pump.PollOne();
                        co_await kYield;
                    }
                });

                nursery.Start([&]() -> Co<> {
                    while (!done) {
                        co_await bc_pump.PumpEvent();
                    }
                });

                // Reader tasks
                for (int i = 0; i < 8; ++i) {
                    nursery.Start([&, is_counter = i == 0]() -> Co<> {
                        auto r = bc_pump.channel_.ForRead();
                        while (!done) {
                            auto event = co_await r.Receive();
                            if (is_counter) {
                                ++events_processed;
                                done = events_processed == kIterations;
                            }
                            benchmark::DoNotOptimize(event);
                        }
                    });
                }
                co_return kJoin;
            };
        });
    }
    state.SetItemsProcessed(state.iterations() * kIterations);
}
BENCHMARK(BM_BroadcastChannel)->Setup(DoSetup)->Repetitions(5)->ReportAggregatesOnly(true);

} // namespace

BENCHMARK_MAIN();

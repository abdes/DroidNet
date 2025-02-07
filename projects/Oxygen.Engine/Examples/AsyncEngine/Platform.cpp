
#include <random> // Include the random library
#include <thread>

#include "Platform.h"

#include "Oxygen/OxCo/Awaitables.h"
#include "Oxygen/OxCo/asio.h"

using oxygen::Platform;
using oxygen::platform::AsyncOps;
using oxygen::platform::EventPump;
using oxygen::platform::InputEvents;
using oxygen::platform::WindowManager;

auto AsyncOps::PollOne() -> size_t
{
    return io_.poll();
}

namespace {
// Generate a random number between 0 and 1000
auto MakeEvent() -> int
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 200);
    return dis(gen);
}
}

auto EventPump::PollOne() -> bool
{
    const int random_number = MakeEvent();
    std::this_thread::sleep_for(std::chrono::milliseconds(random_number / 2));
    // Call SDL
    // DLOG_F(INFO, "random number: {}", random_number);
    if (random_number >= 0 && random_number <= 200) {
        wait_for_event_.UnParkAll();
        return true;
    }
    return false;
}

void EventPump::BootStrap(const size_t index)
{
    DCHECK_F(index == 0 || index == 1);
    DLOG_F(1, "awaitable event index: {}", index);
    aw_event_index_ = index;
    aw_events_[aw_event_index_].~Shared();
    new (&aw_events_[aw_event_index_]) co::Shared<SharedProducer>(std::in_place,
        [this]() -> co::Co<PlatformEvent> {
            co_return co_await PumpEvent();
        });
}

auto EventPump::PumpEvent() -> co::Co<PlatformEvent>
{
    co_await wait_for_event_.Park();

    const auto native_event = MakeEvent();
    auto event = PlatformEvent::Create<int>();
    *(event.NativeEventAs<int>()) = native_event;
    DLOG_F(1, "pumped event: {}", native_event);

    // Prepare for the next event
    BootStrap((aw_event_index_ + 1) % 2);

    // Return the pumped event
    co_return event;
}

auto InputEvents::ProcessPlatformEvents() -> co::Co<>
{
    while (true) {
        auto& event = co_await event_pump_->WaitForNextEvent();
        if (event.IsHandled()) {
            continue;
        }
        auto native_event = *event.NativeEventAs<int>();
        if (native_event <= 100) {
            DLOG_F(INFO, ">*< input event: {}", native_event);
            event.SetHandled();
            events_.emplace_back(native_event);
            ResumeAwaiter();
        }
    }
}

auto WindowManager::ProcessPlatformEvents() -> co::Co<>
{
    while (true) {
        auto& event = co_await event_pump_->WaitForNextEvent();
        if (event.IsHandled()) {
            continue;
        }
        auto native_event = *event.NativeEventAs<int>();
        if (native_event > 100 and native_event <= 150) {
            DLOG_F(INFO, "-*- window event: {}", native_event);
            event.SetHandled();
        }
    }
}

Platform::Platform()
{
    AddComponent<AsyncOps>();
    AddComponent<EventPump>();
    AddComponent<WindowManager>();
    AddComponent<InputEvents>();
}

void Platform::Run()
{
    DCHECK_NOTNULL_F(nursery_);

    nursery_->Start(&WindowManager::ProcessPlatformEvents, &GetComponent<WindowManager>());
    nursery_->Start(&InputEvents::ProcessPlatformEvents, &GetComponent<InputEvents>());
}

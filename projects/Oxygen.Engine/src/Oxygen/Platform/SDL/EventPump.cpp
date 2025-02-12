//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/Platform/Platform.h"
#include "Oxygen/Platform/SDL/Wrapper.h"

using oxygen::platform::EventPump;
using namespace std::chrono_literals;

void EventPump::MaybeBootstrap()
{
    static bool bootstrapped { false };

    if (bootstrapped) {
        return;
    }
    DLOG_F(INFO, "BootStrap({})", current_slot_index_);
    current_slot_index_ = 0;
    NextSlot().Initialize(this);
    bootstrapped = true;
}

auto EventPump::PollOne() -> bool
{
    std::this_thread::sleep_for(10ms);
    if (sdl::PollEvent(nullptr)) {
        poll_.UnParkAll();
        return true;
    }
    return false;
}

auto EventPump::PumpEvent() -> co::Co<PlatformEvent>
{
    co_await AllOf(
        poll_.Park(),
        [this]() -> co::Co<> {
            auto _ = co_await CurrentSlot().Lock();
        });
    auto event = PlatformEvent::Create<SDL_Event>();
    auto* sdl_event = event.NativeEventAs<SDL_Event>();
    const auto got_one = sdl::PollEvent(sdl_event);
    DCHECK_F(got_one); // There should always be an event

    // Prepare the next slot
    current_slot_index_ ^= 1;
    NextSlot().Initialize(this);

    // Return the pumped event
    co_return event;
}

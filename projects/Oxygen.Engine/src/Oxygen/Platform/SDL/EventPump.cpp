//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/SDL/Wrapper.h>

using oxygen::platform::EventPump;
using namespace std::chrono_literals;

EventPump::EventPump()
    : event_source_([this]() -> co::Co<PlatformEvent> {
        co_await poll_.Park();
        auto event = PlatformEvent::Create<SDL_Event>();
        auto* sdl_event = event.NativeEventAs<SDL_Event>();
        const auto got_one = sdl::PollEvent(sdl_event);
        DCHECK_F(got_one); // There should always be an event
        co_return std::move(event);
    })
{
    DLOG_F(1, "Platform event pump created");
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

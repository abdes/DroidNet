//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Platform.h"
#include "Oxygen/Platform/SDL/Wrapper.h"

using oxygen::platform::EventPump;

auto EventPump::PollOne() -> bool
{
    if (sdl::PollEvent(nullptr)) {
        wait_for_event_.UnParkAll();
        return true;
    }
    return false;
}

auto EventPump::PumpEvent() -> co::Co<PlatformEvent>
{
    co_await wait_for_event_.Park();

    auto event = PlatformEvent::Create<SDL_Event>();
    auto* sdl_event = event.NativeEventAs<SDL_Event>();
    const auto got_one = sdl::PollEvent(sdl_event);
    DCHECK_F(got_one); // There should always be an event

    // Prepare for the next event
    BootStrap(aw_event_index_ ^ 1);

    // Return the pumped event
    co_return event;
}

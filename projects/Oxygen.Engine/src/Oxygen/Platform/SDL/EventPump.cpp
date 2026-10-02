//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <SDL3/SDL_events.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/PlatformEvent.h>
#include <Oxygen/Platform/SDL/Wrapper.h>

using oxygen::platform::EventPump;

EventPump::EventPump()
  : event_source_([this]() -> co::Co<PlatformEvent> {
    co_await poll_.Park();
    auto event = PlatformEvent::Create<SDL_Event>();

    if (shutdown_requested_.load()) {
      // Don't poll SDL anymore, just return an empty event
      co_return std::move(event);
    }

    auto* sdl_event = event.NativeEventAs<SDL_Event>();
    const auto got_one = sdl::PollEvent(sdl_event);
    if (!got_one) { // There should always be an event
      ABORT_F("Expected a queued SDL event");
    }
    co_return std::move(event);
  })
{
  DLOG_F(INFO, "Platform event pump created");
}

auto EventPump::PollOne() -> bool
{
  if (sdl::PollEvent(nullptr)) {
    poll_.UnParkAll();
    return true;
  }
  return false;
}

auto EventPump::Shutdown() -> void
{
  // Set the shutdown flag to prevent new coroutines from suspending
  shutdown_requested_.store(true);

  // Wake up any coroutines that are currently parked waiting for events
  // They will see the shutdown flag and complete immediately
  poll_.UnParkAll();

  DLOG_F(
    INFO, "EventPump shutdown requested -> no more events will be processed");
}

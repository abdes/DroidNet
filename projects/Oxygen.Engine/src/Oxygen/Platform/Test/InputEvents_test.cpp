//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/Platform.h>

#include "../../OxCo/Test/Utils/OxCoTestFixture.h"

using oxygen::Platform;
using oxygen::PlatformConfig;

using namespace std::chrono_literals;

namespace co = oxygen::co;
using co::testing::OxCoTestFixture;

class InputEventsTest : public OxCoTestFixture {
protected:
  PlatformConfig config_ { .headless = true };
};

// NOLINTNEXTLINE
NOLINT_TEST_F(InputEventsTest, HeadlessModeExposesWriterAndAllowsInjectedEvents)
{
  using oxygen::SubPixelMotion;
  using oxygen::SubPixelPosition;
  using oxygen::platform::MouseMotionEvent;
  using oxygen::time::PhysicalTime;

  Platform platform(config_);
  auto& input_events = platform.Input();

  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*el_, [&]() -> co::Co<> {
    auto reader = input_events.ForRead();
    auto& writer = input_events.ForWrite();

    constexpr auto motion_x = 10.0F;
    constexpr auto motion_y = 20.0F;
    auto event = std::make_shared<MouseMotionEvent>(PhysicalTime {}, 0,
      SubPixelPosition { .x = 0.0F, .y = 0.0F },
      SubPixelMotion { .dx = motion_x, .dy = motion_y });

    co_await writer.Send(event);

    auto received = co_await reader.Receive();
    EXPECT_NE(received, nullptr);
    EXPECT_EQ(received->GetTypeId(), MouseMotionEvent::ClassTypeId());

    auto mouse_event = std::static_pointer_cast<MouseMotionEvent>(received);
    EXPECT_EQ(mouse_event->GetMotion().dx, motion_x);
    EXPECT_EQ(mouse_event->GetMotion().dy, motion_y);
  });
}

// NOLINTNEXTLINE
NOLINT_TEST_F(InputEventsTest, PlatformComposesCorrectlyInHeadlessMode)
{
  using oxygen::platform::EventPump;
  using oxygen::platform::InputEvents;

  Platform platform(config_);
  EXPECT_TRUE(platform.HasComponent<InputEvents>());
  EXPECT_TRUE(platform.HasComponent<EventPump>());
}

// NOLINTNEXTLINE
NOLINT_TEST_F(InputEventsTest, PlatformShutdownClosesInputEndpoints)
{
  using oxygen::SubPixelMotion;
  using oxygen::SubPixelPosition;
  using oxygen::platform::InputEvents;
  using oxygen::platform::MouseMotionEvent;
  using oxygen::time::PhysicalTime;

  // Create platform and attach a reader/writer
  Platform platform(config_);
  auto& input_events = platform.Input();

  // NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)
  co::Run(*el_, [&]() -> co::Co<> {
    // Reader will be blocked waiting for events
    auto reader = input_events.ForRead();
    auto& writer = input_events.ForWrite();

    // Start a coroutine waiting on Receive(); it should be woken with nullptr
    bool reader_unblocked = false;
    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      n.Start([&]() -> co::Co<> {
        if (const auto received = co_await reader.Receive();
          received == nullptr) {
          reader_unblocked = true;
        }
        co_return;
      });

      // Close the input endpoints to simulate platform shutdown: the input
      // writer should be closed and any blocked readers unblocked.
      // Close the writer explicitly then destroy the platform scope.
      writer.Close();

      // Give the event loop a moment to process the wake-ups
      constexpr auto sleep_duration = 5ms;
      co_await el_->Sleep(sleep_duration);

      EXPECT_TRUE(reader_unblocked);
      co_return oxygen::co::kJoin;
    };
  });
  // NOLINTEND(*-avoid-capturing-lambda-coroutines)
}

// NOLINTNEXTLINE
NOLINT_TEST_F(InputEventsTest, BoundedQueueBackpressureBehavior)
{
  using oxygen::SubPixelMotion;
  using oxygen::SubPixelPosition;
  using oxygen::platform::InputEvents;
  using oxygen::platform::MouseMotionEvent;
  using oxygen::time::PhysicalTime;

  Platform platform(config_);
  auto& input_events = platform.Input();

  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  ::co::Run(*el_, [&]() -> co::Co<> {
    auto reader = input_events.ForRead();
    auto& writer = input_events.ForWrite();
    // Fill up the channel to its capacity using non-blocking TrySend to avoid
    // deadlocking the test harness. Verify the writer reports full afterward.
    for (size_t i = 0; i < InputEvents::kMaxBufferedEvents; ++i) {
      auto sent = writer.TrySend(std::make_shared<MouseMotionEvent>(
        PhysicalTime {}, 0, SubPixelPosition { .x = 0.0F, .y = 0.0F },
        SubPixelMotion { .dx = static_cast<float>(i), .dy = 0.0F }));
      EXPECT_TRUE(sent);
    }

    // Channel should now be full; TrySend should fail for an extra message.
    EXPECT_TRUE(writer.Full());
    EXPECT_FALSE(writer.TrySend(std::make_shared<MouseMotionEvent>(
      PhysicalTime {}, 0, SubPixelPosition { .x = 0.0F, .y = 0.0F },
      SubPixelMotion { .dx = 999.0F, .dy = 0.0F })));

    // Consume a single event to free space; subsequent TrySend should succeed.
    const auto consumed = co_await reader.Receive();
    EXPECT_NE(consumed, nullptr);

    EXPECT_TRUE(writer.TrySend(std::make_shared<MouseMotionEvent>(
      PhysicalTime {}, 0, SubPixelPosition { .x = 0.0F, .y = 0.0F },
      SubPixelMotion { .dx = 999.0F, .dy = 0.0F })));
  });
}

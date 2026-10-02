//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include "../../OxCo/Test/Utils/OxCoTestFixture.h"
#include "../../OxCo/Test/Utils/TestEventLoop.h"
#include <SDL3/SDL_video.h>

#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace co = oxygen::co;
using oxygen::platform::Window;

auto HiddenWindowProperties() -> oxygen::platform::window::Properties
{
  oxygen::platform::window::Properties props { "Window lifecycle test" };
  props.extent
    = oxygen::platform::window::ExtentT { .width = 64, .height = 64 };
  props.flags.hidden = true;
  return props;
}

class WindowTest : public co::testing::OxCoTestFixture {
protected:
  static auto VoteOnClose(Window* window, const bool accept) -> co::Co<>
  {
    co_await window->CloseRequested();
    if (accept) {
      window->VoteToClose();
    } else {
      window->VoteNotToClose();
    }
  }

  static auto DrainWindowEvents(oxygen::Platform* platform) -> co::Co<>
  {
    while (platform->Events().PollOne()) {
      co_await co::kYield;
    }
    co_await co::kYield;
  }

  auto VerifyCloseVotes(oxygen::Platform* platform, co::Nursery* nursery)
    -> co::Co<co::detail::NurseryBodyRetVal>
  {
    co_await nursery->Start(&oxygen::Platform::ActivateAsync, platform);
    platform->Run();
    auto window
      = platform->Windows().MakeWindow(HiddenWindowProperties()).lock();
    EXPECT_NE(window, nullptr);
    if (!window) {
      co_return co::kCancel;
    }

    nursery->Start(VoteOnClose, window.get(), false);
    co_await co::kYield;
    window->RequestClose();
    co_await DrainWindowEvents(platform);
    // Drain ready coroutine completions before beginning the next frame.
    co_await el_->Sleep(std::chrono::milliseconds::zero());
    platform->OnFrameEnd();
    EXPECT_TRUE(platform->Windows().GetPendingCloses().empty());

    nursery->Start(VoteOnClose, window.get(), true);
    co_await co::kYield;
    window->RequestClose();
    co_await DrainWindowEvents(platform);
    // Drain ready coroutine completions before beginning the next frame.
    co_await el_->Sleep(std::chrono::milliseconds::zero());
    platform->OnFrameEnd();
    EXPECT_EQ(platform->Windows().GetPendingCloses().size(), 1U);
    EXPECT_NE(SDL_GetWindowFromID(window->Id()), nullptr);

    platform->OnFrameStart();
    EXPECT_EQ(SDL_GetWindowFromID(window->Id()), nullptr);
    platform->OnFrameStart();
    platform->Stop();
    co_return co::kJoin;
  }

  auto RunCloseVotes(oxygen::Platform* platform) -> co::Co<>
  {
    co_yield co::Nursery::Factory {} %
      [this, platform](
        co::Nursery& nursery) -> co::Co<co::detail::NurseryBodyRetVal> {
      return VerifyCloseVotes(platform, &nursery);
    };
  }
};

NOLINT_TEST_F(WindowTest, VetoThenAcceptDefersNativeDestruction)
{
  oxygen::Platform platform(oxygen::PlatformConfig { .headless = true });
  co::Run(*el_, RunCloseVotes(&platform));
}

NOLINT_TEST_F(WindowTest, OwnerReleasesNativeWindow)
{
  oxygen::Platform platform(oxygen::PlatformConfig { .headless = true });
  oxygen::platform::WindowIdType id {};
  {
    Window window(HiddenWindowProperties());
    id = window.Id();
    ASSERT_NE(SDL_GetWindowFromID(id), nullptr);
  }
  EXPECT_EQ(SDL_GetWindowFromID(id), nullptr);
}

} // namespace

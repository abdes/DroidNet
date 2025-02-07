//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Window.h"

#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Oxygen/Platform/Types.h"

using testing::Eq;

using oxygen::PixelExtent;
using oxygen::PixelPosition;
using oxygen::platform::NativeWindowInfo;
using oxygen::platform::Window;
using oxygen::platform::WindowIdType;

class MockWindow final : public Window {
public:
    MockWindow() = default;
    ~MockWindow() override = default;

    OXYGEN_MAKE_NON_COPYABLE(MockWindow);
    OXYGEN_MAKE_NON_MOVEABLE(MockWindow);

    // clang-format off
  MOCK_METHOD(WindowIdType, Id, (), (const, override));
  MOCK_METHOD(NativeWindowInfo, NativeWindow, (), (const, override));
  MOCK_METHOD(bool, IsValid, (), (const, override));
  MOCK_METHOD(bool, IsBorderLess, (), (const, override));
  MOCK_METHOD(bool, IsFullScreen, (), (const, override));
  MOCK_METHOD(bool, IsMaximized, (), (const, override));
  MOCK_METHOD(bool, IsMinimized, (), (const, override));
  MOCK_METHOD(bool, IsResizable, (), (const, override));
  MOCK_METHOD(PixelExtent, Size, (), (const, override));
  MOCK_METHOD(PixelPosition, Position, (), (const, override));
  MOCK_METHOD(std::string, Title, (), (const, override));
  MOCK_METHOD(PixelExtent, GetFrameBufferSize, (), (const, override));
  MOCK_METHOD(void, Activate, (), (override));
  MOCK_METHOD(void, AlwaysOnTop, (bool always_on_top), (override));
  MOCK_METHOD(void, FullScreen, (bool full_screen), (override));
  MOCK_METHOD(void, Hide, (), (override));
  MOCK_METHOD(void, DoMaximize, (), (override));
  MOCK_METHOD(void, MaximumSize, (PixelExtent const& extent), (override));
  MOCK_METHOD(void, Minimize, (), (override));
  MOCK_METHOD(void, MinimumSize, (PixelExtent const& extent), (override));
  MOCK_METHOD(void, DoPosition, (PixelPosition const& position), (override));
  MOCK_METHOD(void, Resizable, (bool full_screen), (override));
  MOCK_METHOD(void, DoRestore, (), (override));
  MOCK_METHOD(void, Show, (), (override));
  MOCK_METHOD(void, DoResize, (PixelExtent const& extent), (override));
  MOCK_METHOD(void, Title, (std::string const& title), (override));
  MOCK_METHOD(void, ProcessCloseRequest, (bool), (override));
    // clang-format on
};

TEST(WindowTest, RequestCloseNoForce)
{
    MockWindow window;

    EXPECT_CALL(window, ProcessCloseRequest(false)).Times(1);

    auto subscription = window.OnCloseRequested().connect(
        [](const bool force) {
            EXPECT_FALSE(force);
        });
    window.RequestClose();
    EXPECT_TRUE(window.ShouldClose());
    subscription.disconnect();
}

TEST(WindowTest, RequestCloseNoForceRejected)
{
    MockWindow window;
    auto subscription = window.OnCloseRequested().connect(
        [&window](const bool force) {
            EXPECT_FALSE(force);
            window.RequestNotToClose();
        });
    window.RequestClose();
    EXPECT_FALSE(window.ShouldClose());
    subscription.disconnect();
}

TEST(WindowTest, RequestCloseForce)
{
    MockWindow window;

    EXPECT_CALL(window, ProcessCloseRequest(true)).Times(1);

    auto subscription = window.OnCloseRequested().connect(
        [](const bool force) {
            EXPECT_TRUE(force);
        });
    window.RequestClose(true);
    EXPECT_TRUE(window.ShouldClose());
    subscription.disconnect();
}

TEST(WindowTest, RequestCloseForceRejected)
{
    MockWindow window;

    EXPECT_CALL(window, ProcessCloseRequest(true)).Times(1);

    auto subscription = window.OnCloseRequested().connect(
        [&window](const bool force) {
            EXPECT_TRUE(force);
            window.RequestNotToClose();
        });
    window.RequestClose(true);
    EXPECT_TRUE(window.ShouldClose());
    subscription.disconnect();
}

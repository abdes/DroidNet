//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/platform/input.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#if defined(ASAP_IS_DEBUG_BUILD)
# include <asap/contract/ut/gtest.h>
#endif

#include "oxygen/platform/platform.h"
#include "oxygen/platform/types.h"

using oxygen::Platform;
using oxygen::platform::InputSlots;

using testing::Eq;
using testing::IsFalse;
using testing::IsTrue;

// NOLINTBEGIN
class MockPlatform : public Platform
{
 public:
  MockPlatform() = default;
  ~MockPlatform() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MockPlatform)
  OXYGEN_MAKE_NON_MOVEABLE(MockPlatform)
  // clang-format off
  MOCK_METHOD(std::weak_ptr<oxygen::platform::Window>, MakeWindow, (std::string const&, oxygen::PixelExtent const&), (override));
  MOCK_METHOD(std::weak_ptr<oxygen::platform::Window>, MakeWindow, (std::string const&, oxygen::PixelExtent const&, oxygen::platform::Window::InitialFlags), (override));
  MOCK_METHOD(std::weak_ptr<oxygen::platform::Window>, MakeWindow, (std::string const&, oxygen::PixelPosition const&, oxygen::PixelExtent const&), (override));
  MOCK_METHOD(std::weak_ptr<oxygen::platform::Window>, MakeWindow, (std::string const&, oxygen::PixelPosition const&, oxygen::PixelExtent const&, oxygen::platform::Window::InitialFlags), (override));
  MOCK_METHOD(std::unique_ptr<oxygen::platform::InputEvent>, PollEvent, (), (override));
  MOCK_METHOD(std::vector<const char*>, GetRequiredInstanceExtensions, (), (const, override));
  MOCK_METHOD(std::vector<std::unique_ptr<oxygen::platform::Display>>, Displays, (), (const, override));
  MOCK_METHOD(std::unique_ptr<oxygen::platform::Display>, DisplayFromId, (const oxygen::platform::Display::IdType&), (const, override));
  // clang-format on
};
// NOLINTEND

// NOLINTNEXTLINE
TEST(InputSlots, KeySlotProperties)
{
  const auto platform = std::make_shared<MockPlatform>();
  const auto& slot = platform->GetInputSlotForKey(oxygen::platform::Key::kH);
  EXPECT_THAT(slot.IsKeyboardKey(), IsTrue());
  EXPECT_THAT(slot.IsModifierKey(), IsFalse());
  EXPECT_THAT(slot.IsAxis1D(), IsFalse());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kKeyCategoryName));
}

// NOLINTNEXTLINE
TEST(InputSlots, ModifierKeySlotProperties)
{
  const auto platform = std::make_shared<MockPlatform>();
  const auto& slot =
      platform->GetInputSlotForKey(oxygen::platform::Key::kLeftAlt);
  EXPECT_THAT(slot.IsKeyboardKey(), IsTrue());
  EXPECT_THAT(slot.IsModifierKey(), IsTrue());
  EXPECT_THAT(slot.IsAxis1D(), IsFalse());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kKeyCategoryName));
}

// NOLINTNEXTLINE
TEST(InputSlots, MouseButtonSlotProperties)
{
  const auto platform = std::make_shared<MockPlatform>();
  const auto& slot = InputSlots::LeftMouseButton;
  EXPECT_THAT(slot.IsKeyboardKey(), IsFalse());
  EXPECT_THAT(slot.IsModifierKey(), IsFalse());
  EXPECT_THAT(slot.IsMouseButton(), IsTrue());
  EXPECT_THAT(slot.IsAxis1D(), IsFalse());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kMouseCategoryName));
}

// NOLINTNEXTLINE
TEST(InputSlots, MouseButtonAxisSlotProperties)
{
  const auto platform = std::make_shared<MockPlatform>();
  const auto& slot = InputSlots::MouseX;
  EXPECT_THAT(slot.IsKeyboardKey(), IsFalse());
  EXPECT_THAT(slot.IsModifierKey(), IsFalse());
  EXPECT_THAT(slot.IsMouseButton(), IsTrue());
  EXPECT_THAT(slot.IsAxis1D(), IsTrue());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kMouseCategoryName));
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/platform/input.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "oxygen/platform/Types.h"

using oxygen::platform::InputSlots;

using testing::Eq;
using testing::IsFalse;
using testing::IsTrue;

// NOLINTNEXTLINE
TEST(InputSlots, KeySlotProperties) {
  InputSlots::Initialize();
  const auto &slot = InputSlots::GetInputSlotForKey(oxygen::platform::Key::kH);
  EXPECT_THAT(slot.IsKeyboardKey(), IsTrue());
  EXPECT_THAT(slot.IsModifierKey(), IsFalse());
  EXPECT_THAT(slot.IsAxis1D(), IsFalse());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kKeyCategoryName));
}

// NOLINTNEXTLINE
TEST(InputSlots, ModifierKeySlotProperties) {
  InputSlots::Initialize();
  const auto &slot =
      InputSlots::GetInputSlotForKey(oxygen::platform::Key::kLeftAlt);
  EXPECT_THAT(slot.IsKeyboardKey(), IsTrue());
  EXPECT_THAT(slot.IsModifierKey(), IsTrue());
  EXPECT_THAT(slot.IsAxis1D(), IsFalse());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kKeyCategoryName));
}

// NOLINTNEXTLINE
TEST(InputSlots, MouseButtonSlotProperties) {
  InputSlots::Initialize();
  const auto &slot = InputSlots::LeftMouseButton;
  EXPECT_THAT(slot.IsKeyboardKey(), IsFalse());
  EXPECT_THAT(slot.IsModifierKey(), IsFalse());
  EXPECT_THAT(slot.IsMouseButton(), IsTrue());
  EXPECT_THAT(slot.IsAxis1D(), IsFalse());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kMouseCategoryName));
}

// NOLINTNEXTLINE
TEST(InputSlots, MouseButtonAxisSlotProperties) {
  InputSlots::Initialize();
  const auto &slot = InputSlots::MouseX;
  EXPECT_THAT(slot.IsKeyboardKey(), IsFalse());
  EXPECT_THAT(slot.IsModifierKey(), IsFalse());
  EXPECT_THAT(slot.IsMouseButton(), IsTrue());
  EXPECT_THAT(slot.IsAxis1D(), IsTrue());
  EXPECT_THAT(slot.IsAxis2D(), IsFalse());
  EXPECT_THAT(slot.IsAxis3D(), IsFalse());
  EXPECT_THAT(slot.GetInputCategoryName(), Eq(InputSlots::kMouseCategoryName));
}

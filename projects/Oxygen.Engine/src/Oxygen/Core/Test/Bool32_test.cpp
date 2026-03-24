//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Bool32.h>

namespace {

using oxygen::Bool32;

TEST(Bool32Test, DefaultsToFalseWithShaderSafeStorageSize)
{
  constexpr auto value = Bool32 {};

  static_assert(sizeof(Bool32) == 4U);
  static_assert(alignof(Bool32) == alignof(std::uint32_t));

  EXPECT_FALSE(static_cast<bool>(value));
  EXPECT_EQ(value.RawValue(), 0U);
}

TEST(Bool32Test, StoresOnlyBooleanSemantics)
{
  constexpr auto false_value = Bool32 { false };
  constexpr auto true_value = Bool32 { true };

  EXPECT_FALSE(static_cast<bool>(false_value));
  EXPECT_TRUE(static_cast<bool>(true_value));
  EXPECT_EQ(false_value.RawValue(), 0U);
  EXPECT_EQ(true_value.RawValue(), 1U);
}

TEST(Bool32Test, EqualityFollowsBooleanMeaning)
{
  EXPECT_EQ(Bool32 { true }, Bool32 { true });
  EXPECT_EQ(Bool32 { false }, Bool32 { false });
  EXPECT_NE(Bool32 { true }, Bool32 { false });
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/TextWrap/TextWrap.h>

using ::testing::Eq;
using ::testing::IsTrue;

namespace oxygen::wrap {

namespace {

  // NOLINTNEXTLINE
  TEST(TextWrapperSpecialCasesTest, EmptyString)
  {
    constexpr size_t column_width = 30;
    const auto* text = "";
    const TextWrapper wrapper = oxygen::wrap::MakeWrapper().Width(column_width);
    EXPECT_THAT(wrapper.Fill(text).value(), Eq(""));
  }

  // NOLINTNEXTLINE
  TEST(TextWrapperSpecialCasesTest, Space)
  {
    constexpr size_t column_width = 30;
    const auto* text = " ";
    const TextWrapper wrapper = oxygen::wrap::MakeWrapper().Width(column_width);
    EXPECT_THAT(wrapper.Fill(text).value(), Eq(" "));
  }

  // NOLINTNEXTLINE
  TEST(TextWrapperSpecialCasesTest, EmptyLine)
  {
    constexpr size_t column_width = 30;
    const auto* text = "\n";
    const TextWrapper wrapper = oxygen::wrap::MakeWrapper().Width(column_width);
    EXPECT_THAT(wrapper.Fill(text).value(), Eq(""));
  }

  // NOLINTNEXTLINE
  TEST(TextWrapperSpecialCasesTest, OneShortWord)
  {
    constexpr size_t column_width = 30;
    const auto* text = "hello";
    const TextWrapper wrapper = oxygen::wrap::MakeWrapper().Width(column_width);
    EXPECT_THAT(wrapper.Fill(text).value(), Eq("hello"));
  }

  // NOLINTNEXTLINE
  TEST(TextWrapperSpecialCasesTest, OneLongWord)
  {
    constexpr size_t column_width = 5;
    const auto* text = "unequivocally";
    const TextWrapper wrapper = oxygen::wrap::MakeWrapper().Width(column_width);
    EXPECT_THAT(wrapper.Fill(text).value(), Eq("unequivocally"));
  }
  // NOLINTNEXTLINE
  TEST(TextWrapperSpecialCasesTest, EndingWithNewLineAndSpace)
  {
    constexpr size_t column_width = 10;
    const auto* text = "q\'4\"k4AqFX.  |kWPF{Yh>,\v\t";
    const TextWrapper wrapper = oxygen::wrap::MakeWrapper()
                                  .Width(column_width)
                                  .CollapseWhiteSpace()
                                  .BreakOnHyphens()
                                  .TrimLines();
    EXPECT_THAT(wrapper.Fill(text).has_value(), IsTrue());
  }

} // namespace

} // namespace oxygen::wrap

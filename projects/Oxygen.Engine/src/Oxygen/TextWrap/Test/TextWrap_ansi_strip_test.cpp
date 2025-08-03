//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/TextWrap/TextWrap.h>

using oxygen::wrap::MakeWrapper;
using oxygen::wrap::TextWrapper;

using ::testing::Eq;

namespace {

/*!
 Verifies that when IgnoreAnsiEscapeCodes is enabled, ANSI escape codes are
 ignored for width calculation but preserved in output. Covers normal, edge,
 and formatting scenarios.
*/
TEST(TextWrapAnsiTest, IgnoreAnsiEscapeCodes_StripsCodesFromWidth)
{

  // Arrange
  const std::string ansi_red = "\x1B[31m";
  const std::string ansi_reset = "\x1B[0m";
  std::string input
    = ansi_red + "Hello" + ansi_reset + " " + ansi_red + "World" + ansi_reset;
  const TextWrapper wrapper = MakeWrapper().Width(5).IgnoreAnsiEscapeCodes();

  // Act
  auto lines_opt = wrapper.Wrap(input);

  // Assert
  ASSERT_TRUE(lines_opt.has_value());
  const auto& lines = lines_opt.value();
  using ::testing::ElementsAre;
  EXPECT_THAT(lines,
    ElementsAre(
      ansi_red + "Hello" + ansi_reset, " ", ansi_red + "World" + ansi_reset));
}

/*!
 Verifies that complex ANSI sequences (multiple parameters) are ignored
 for width calculation.
*/
TEST(TextWrapAnsiTest, IgnoreAnsiEscapeCodes_ComplexSequences)
{
  // Arrange
  const std::string ansi_bold_red = "\x1B[1;31m";
  const std::string ansi_reset = "\x1B[0m";
  std::string input = ansi_bold_red + "BoldRed" + ansi_reset + " "
    + ansi_bold_red + "Text" + ansi_reset;
  const TextWrapper wrapper = MakeWrapper().Width(7).IgnoreAnsiEscapeCodes();

  // Act
  auto lines_opt = wrapper.Wrap(input);

  // Assert
  ASSERT_TRUE(lines_opt.has_value());
  const auto& lines = lines_opt.value();
  using ::testing::ElementsAre;
  EXPECT_THAT(lines,
    ElementsAre(ansi_bold_red + "BoldRed" + ansi_reset,
      " " + ansi_bold_red + "Text" + ansi_reset));
}

/*!
 Verifies that visible width is correct when ANSI codes are present and
 ignored.
*/
TEST(TextWrapAnsiTest, IgnoreAnsiEscapeCodes_WidthCalculation)
{
  // Arrange
  const std::string ansi_green = "\x1B[32m";
  const std::string ansi_reset = "\x1B[0m";
  std::string input = ansi_green + "abcde" + ansi_reset + " " + ansi_green
    + "fghij" + ansi_reset;
  const TextWrapper wrapper = MakeWrapper().Width(5).IgnoreAnsiEscapeCodes();

  // Act
  auto lines_opt = wrapper.Wrap(input);

  // Assert
  ASSERT_TRUE(lines_opt.has_value());
  const auto& lines = lines_opt.value();
  using ::testing::ElementsAre;
  EXPECT_THAT(lines,
    ElementsAre(ansi_green + "abcde" + ansi_reset, " ",
      ansi_green + "fghij" + ansi_reset));
}

/*!
 Verifies that empty input returns an empty result, even with ANSI codes
 enabled.
*/
TEST(TextWrapAnsiTest, IgnoreAnsiEscapeCodes_EmptyInput)
{
  // Arrange
  const TextWrapper wrapper = MakeWrapper().Width(10).IgnoreAnsiEscapeCodes();

  // Act
  auto lines_opt = wrapper.Wrap("");

  // Assert
  ASSERT_TRUE(lines_opt.has_value());
  using ::testing::IsEmpty;
  EXPECT_THAT(*lines_opt, IsEmpty());
}

} // namespace

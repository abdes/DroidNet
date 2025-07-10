//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/TextWrap/TextWrap.h>

namespace {

// NOLINTNEXTLINE
TEST(TextWrapperExamples, TypicalWrapWithLineBreaksAndCollapsedSpaces)
{
  using oxygen::wrap::TextWrapper;

  //! [Example usage]
  const auto* passage = R"(Pride and Prejudice:
    It is a truth universally acknowledged, that a single man in possession
    of a good fortune, must be in want of a wife.)";

  constexpr size_t column_width = 28;
  const TextWrapper wrapper = oxygen::wrap::MakeWrapper()
                                .Width(column_width)
                                .TrimLines()
                                .CollapseWhiteSpace()
                                .IndentWith()
                                .Initially("")
                                .Then("   ");

  std::cout << wrapper.Fill(passage).value_or("error") << "\n";

  // Pride and Prejudice:
  //    It is a truth universally
  //    acknowledged, that a
  //    single man in possession
  //    of a good fortune, must
  //    be in want of a wife.
  //! [Example usage]
}

} // namespace

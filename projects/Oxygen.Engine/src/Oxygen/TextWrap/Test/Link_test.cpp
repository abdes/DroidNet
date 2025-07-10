//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <iostream>

#include <Oxygen/TextWrap/TextWrap.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  using asap::wrap::TextWrapper;
  using asap::wrap::TextWrapperBuilder;

  TextWrapperBuilder builder;
  const TextWrapper wrapper = asap::wrap::MakeWrapper()
                                .Width(60)
                                .TrimLines()
                                .CollapseWhiteSpace()
                                .IndentWith();

  std::string text = "This is a sample text that will be wrapped according to "
                     "the specified parameters (60 columns). "
                     "It includes some long words and should demonstrate the "
                     "functionality of the TextWrapper.";

  auto wrapped = wrapper.Fill(text);
  if (wrapped.has_value()) {
    std::cout << *wrapped << "\n";
    return EXIT_SUCCESS;
  }
  std::cerr << "An error occurred while wrapping the text\n";
  return EXIT_FAILURE;
}

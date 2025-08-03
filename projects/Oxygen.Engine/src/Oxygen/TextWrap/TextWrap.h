//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <Oxygen/TextWrap/api_export.h>

namespace oxygen::wrap {

//! Forward declaration for the TextWrapperBuilder class.
class TextWrapperBuilder;

//! The core class for the text wrapper module.
/*!
 TextWrapper provides a configurable interface for wrapping text to a specific
 width, supporting options for whitespace handling, indentation, tab expansion,
 and hyphen-based word breaking. The wrapping algorithm uses dynamic programming
 to minimize raggedness, penalizing extra spaces at line ends for balanced
 output.

 Instances must be constructed via TextWrapperBuilder, which ensures coherent
 configuration and a fluent API for setup.

 ### Usage Examples

 ```cpp
 std::string input = "Hello world.\n\nThis is Oxygen.";
 auto wrapper = oxygen::wrap::MakeWrapper().Width(10);
 auto lines = wrapper.Wrap(input);
 // Output: {"Hello", "world.", "", "This is", "Oxygen."}
 ```

 @see TextWrapperBuilder, Wrap, Fill, TextWrap.cpp
 @warning Direct construction is not allowed; use the builder.
*/
class TextWrapper {
public:
  friend class TextWrapperBuilder;

  //! Wraps text to the configured width using a cost-optimized algorithm.
  OXGN_TXW_NDAPI auto Wrap(const std::string& str) const
    -> std::optional<std::vector<std::string>>;

  //! Wraps text and returns a single string containing the result.
  OXGN_TXW_NDAPI auto Fill(const std::string& str) const
    -> std::optional<std::string>;

  OXGN_TXW_NDAPI auto to_string() const -> std::string;

private:
  TextWrapper() = default;

  size_t width_ { DEFAULT_COLUMN_WIDTH };

  std::string indent_;
  std::string initial_indent_ {};

  std::string tab_ { DEFAULT_TAB_EXPANSION };

  bool collapse_ws_ { false };
  bool trim_lines_ { false };

  bool break_on_hyphens_ { false };

  bool ignore_ansi_escape_codes_ { false };

  static constexpr size_t DEFAULT_COLUMN_WIDTH = 80;
  static constexpr auto DEFAULT_TAB_EXPANSION = "\t";
};

//! Returns a string representation of the TextWrapper configuration.
inline auto to_string(const TextWrapper& wrapper) -> std::string
{
  return wrapper.to_string();
}

//! A fluent interface builder for TextWrapper.
/*!
  TextWrapperBuilder simplifies the creation and configuration of TextWrapper
  instances through a fluent API. All configuration methods return a reference
  to the builder for chaining. The builder maintains mutable state until
  conversion to TextWrapper.

  ### Usage Examples

  ```cpp
  auto wrapper = oxygen::wrap::MakeWrapper()
    .Width(60)
    .IndentWith().Initially("  ").Then("    ");
  ```

  @see TextWrapper, MakeWrapper
*/
class TextWrapperBuilder {
public:
  TextWrapperBuilder() = default;

  //! Converts a TextWrapperBuilder instance to a TextWrapper instance.
  /*!
    Converts a TextWrapperBuilder instance to a TextWrapper instance at the end
    of the building process. The builder is left in a valid but unspecified
    state.

    @return An instance of TextWrapper.
    @see TextWrapper
    @note Implicit conversion; use at the end of a builder chain.
  */
  /* implicit */ operator TextWrapper() const { return std::move(wrapper); }

  //! Sets the maximum length of wrapped lines.
  OXGN_TXW_API auto Width(size_t width) -> TextWrapperBuilder&;

  //! Starts the fluent API for setting indentation prefixes.
  OXGN_TXW_API auto IndentWith() -> TextWrapperBuilder&;

  //! Sets the string prepended to the first line of wrapped output.
  OXGN_TXW_API auto Initially(std::string initial_indent)
    -> TextWrapperBuilder&;

  //! Sets the string prepended to all lines except the first.
  OXGN_TXW_API auto Then(std::string indent) -> TextWrapperBuilder&;

  //! Expands all tab characters in text using the given string.
  OXGN_TXW_API auto ExpandTabs(std::string tab) -> TextWrapperBuilder&;

  //! Replaces contiguous series of white spaces with a single space.
  OXGN_TXW_API auto CollapseWhiteSpace() -> TextWrapperBuilder&;

  //! Trims whitespace at the beginning and end of every line.
  OXGN_TXW_API auto TrimLines() -> TextWrapperBuilder&;

  //! Enables breaking compound words after hyphens.
  OXGN_TXW_API auto BreakOnHyphens() -> TextWrapperBuilder&;

  //! Enables ignoring ANSI escape codes in width calculation.
  OXGN_TXW_API auto IgnoreAnsiEscapeCodes() -> TextWrapperBuilder&;

private:
  mutable TextWrapper wrapper;
};

//! Create a new TextWrapperBuilder instance to start building a TextWrapper.
/*!
  Creates a new TextWrapperBuilder instance to start building a TextWrapper.

  @return A new TextWrapperBuilder instance.
  @see TextWrapperBuilder
*/
inline auto MakeWrapper() -> TextWrapperBuilder { return {}; }

} // namespace oxygen::wrap

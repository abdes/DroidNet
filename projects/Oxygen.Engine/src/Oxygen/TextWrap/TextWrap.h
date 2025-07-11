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

/// Text wrapper public interface.
namespace oxygen::wrap {

class TextWrapperBuilder;

/*!
 * \brief The core class for the text wrapper module.
 *
 * TextWrapper offers a simple interface to wrap text to a specific width while
 * supporting multiple configuration options for how white space is handled and
 * when exactly line/word breaking happen.
 *
 * Construction of instances of TextWrapper can only be done via its associated
 * TextWrapperBuilder, which maintains coherent configuration values and offers
 * an easy to use fluent interface to set them.
 *
 * **Example**
 *
 * \snippet textwrapper_example.cpp Example usage
 *
 * \see TextWrapperBuilder for a detailed description of the different
 * configuration options.
 */
class TextWrapper {
public:
  /*!
   * \brief Wraps text so every line is at most the TextWrapper's `width`
   * characters long.
   *
   * The wrapping algorithm is an improved algorithm that favors balanced lines
   * vs. greedy filling of lines. A cost is associated with each line, using the
   * extra spaces at the end of the line not filed with words. The goal of the
   * algorithm is to minimize the overall cost.
   *
   * \param str a single or multi-paragraph string, where each paragraph is
   * separated from the next one by one or more empty lines.
   * \return a list of output lines, without final newlines. Paragraphs are
   * separated by a single empty line.
   */
  OXGN_TXW_NDAPI auto Wrap(const std::string& str) const
    -> std::optional<std::vector<std::string>>;

  /*!
   * \brief Wraps text so every line is at most the TextWrapper's `width`
   * characters long and returns a single string containing the result.
   *
   * `Fill()` is effectively equivalent to joining the result of `Wrap()` using
   * `\n`;
   *
   * @param str a single or multi-paragraph string, where each paragraph is
   * separated from the next one by one or more empty lines.
   * @return a single string containing the result.
   */
  OXGN_TXW_NDAPI auto Fill(const std::string& str) const
    -> std::optional<std::string>;

  friend class TextWrapperBuilder;
  friend OXGN_TXW_API auto operator<<(
    std::ostream& out, const TextWrapper& wrapper) -> std::ostream&;

private:
  TextWrapper() = default;

  size_t width_ { DEFAULT_COLUMN_WIDTH };

  std::string indent_;
  std::string initial_indent_ {};

  std::string tab_ { DEFAULT_TAB_EXPANSION };

  bool collapse_ws_ { false };
  bool trim_lines_ { false };

  bool break_on_hyphens_ { false };

  static constexpr size_t DEFAULT_COLUMN_WIDTH = 80;
  static constexpr auto DEFAULT_TAB_EXPANSION = "\t";
};

/*!
 * \brief Overloaded output stream operator for pretty printing TextWrapper
 * configuration parameters. values.
 *
 * @param out output stream to which formatted data will be inserted.
 * @param wrapper the TExtWrapper to write to the stream.
 * @return out
 */
OXGN_TXW_API auto operator<<(std::ostream& out, const TextWrapper& wrapper)
  -> std::ostream&;

/*!
 * \brief A fluent interface builder for TextWrapper.
 *
 * This builder simplifies the creation and configuration of TextWrapper
 * instances through a fluent API.
 *
 * **Example**
 *
 * \snippet textwrapper_example.cpp Example usage
 */
class TextWrapperBuilder {
public:
  TextWrapperBuilder() = default;

  /*! \brief converts a TextWrapperBuilder instance to a TextWrapper instance at
   * the end of the building.
   *
   * @return an instance of TExtWrapper.
   */
  /* implicit */ operator TextWrapper() const
  { // NOLINT
    return std::move(wrapper);
  }

  /*!
   * \brief The maximum length of wrapped lines.
   *
   * (default: 80) As long as there are no individual words in the input text
   * longer than width, TextWrapper guarantees that no output line will be
   * longer than width characters.
   */
  OXGN_TXW_API auto Width(size_t width) -> TextWrapperBuilder&;

  /*!
   * \brief Setup indentation prefixes.
   *
   * Starts the fluent API for setting the optional indentation prefixes for the
   * first line and the subsequent lines.
   *
   * \see Initially
   * \see Then
   */
  OXGN_TXW_API auto IndentWith() -> TextWrapperBuilder&;

  /*!
   * \brief String that will be prepended to the first line of wrapped output.
   *
   * (default: '') Counts towards the length of the first line. The empty string
   * is not indented.
   *
   * \see IndentWith
   * \see Then
   */
  OXGN_TXW_API auto Initially(std::string initial_indent)
    -> TextWrapperBuilder&;

  /*!
   * \brief String that will be prepended to all lines of wrapped output except
   * the first.
   *
   * (default: '') Counts towards the length of the first line. The empty string
   * is not indented.
   *
   * \see IndentWith
   * \see Initially
   */
  OXGN_TXW_API auto Then(std::string indent) -> TextWrapperBuilder&;

  /*!
   * \brief All tab characters in text are expanded using the `tab` string.
   *
   * (default: "\t") This is the first transformation that happens before
   * replacing / collapsing white spaces and before wrapping.
   */
  OXGN_TXW_API auto ExpandTabs(std::string tab) -> TextWrapperBuilder&;

  /*!
   * \brief Replace contiguous series of white spaces with a single space.
   *
   * (default: false)
   *
   * \note White space collapsing is done after tab expansion, therefore unless
   * tab expansion is done with non-white space characters, it is superseded.
   */
  OXGN_TXW_API auto CollapseWhiteSpace() -> TextWrapperBuilder&;

  /*!
   * \brief Whitespace at the beginning and ending of every line (after wrapping
   * but before indenting) is dropped.
   *
   * (default: false)
   */
  OXGN_TXW_API auto TrimLines() -> TextWrapperBuilder&;

  /*!
   * \brief Compound words will be broken into separate chunks right after
   * hyphens, as it is customary in English.
   *
   * (default: false)
   */
  OXGN_TXW_API auto BreakOnHyphens() -> TextWrapperBuilder&;

private:
  mutable TextWrapper wrapper;
};

/*!
 * \brief Create a new TextWrapperBuilder instance to start building a
 * TextWrapper.
 */
inline auto MakeWrapper() -> TextWrapperBuilder { return {}; }

} // namespace oxygen::wrap

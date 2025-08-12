//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <regex>
#include <utility>

#include <Oxygen/TextWrap/Internal/Tokenizer.h>
#include <Oxygen/TextWrap/TextWrap.h>

using oxygen::wrap::TextWrapper;
using oxygen::wrap::internal::Token;
using oxygen::wrap::internal::TokenConsumer;
using oxygen::wrap::internal::Tokenizer;
using oxygen::wrap::internal::TokenType;

namespace {

/*!
  Removes ANSI escape codes from a string for width calculation purposes.
  Only CSI (Control Sequence Introducer) codes matching the regex
  "\\x1B\\[[0-9;]*[A-Za-z]" are stripped. This covers standard SGR (Select
  Graphic Rendition) and most formatting/color codes, but does not remove OSC,
  DCS, or other non-CSI sequences.

  @param input The string to process.
  @return The input string with supported ANSI escape codes removed.

  ### Supported ANSI Escape Codes

  - CSI sequences: ESC [ ... letter (e.g., "\x1B[31m", "\x1B[1;32m")
  - SGR (color/style): "\x1B[...m"
  - Other CSI formatting: "\x1B[...K", etc.

  ### Not Supported

  - OSC (Operating System Command): "\x1B]...\x07"
  - DCS (Device Control String): "\x1BP...\x1B\\"
  - Non-CSI ESC codes (e.g., "\x1B7", "\x1B8")

  @note This matches the feature scope and regex used in TextWrap.
*/
auto StripAnsiEscapeCodes(const std::string& input) -> std::string
{
  static const std::regex ansi_regex("\\x1B\\[[0-9;]*[A-Za-z]");
  return std::regex_replace(input, ansi_regex, "");
}

auto WrapChunks(const std::vector<Token>& chunks, const size_t width,
  const std::string& indent, const std::string& initial_indent,
  const bool trim_lines, const bool ignore_ansi_escape_codes)
  -> std::vector<std::string>
{

  // https://www.geeksforgeeks.org/word-wrap-problem-space-optimized-solution/

  const auto num_chunks = chunks.size();
  const auto first_line_width = width - initial_indent.size();
  const auto other_line_width = width - indent.size();

  size_t cur_chunk;
  size_t cur_chunk_in_line;

  // Table in which costs[index] represents cost of line starting with word
  // chunks[index].
  std::vector<size_t> costs(num_chunks);

  // Table in which optimized[index] stores index of the last word in line
  // starting with word chunks[index].
  std::vector<size_t> optimized(num_chunks);

  // If only one word is present then only one line is required. Cost of last
  // line is zero. Hence, cost of this line is zero. Ending point is also n-1 as
  // single word is present.
  costs[num_chunks - 1] = 0;
  optimized[num_chunks - 1] = num_chunks - 1;

  if (num_chunks > 1) {

    // Variable to store possible minimum cost of line.
    size_t cost;

    // Make each word first word of line by iterating over each index in arr.
    cur_chunk = num_chunks - 1;
    do {
      cur_chunk--;

      // Variable to store number of characters in given line.
      size_t current_length { 0 };

      costs[cur_chunk] = (std::numeric_limits<size_t>::max)();
      const auto adjusted_width
        = (cur_chunk == 0 ? first_line_width : other_line_width);

      cur_chunk_in_line = cur_chunk;
      auto first_chunk_in_line = cur_chunk_in_line;

      // The new line token can either have a size that will break the maximum
      // width or a size of zero depending on whether it is the first chunk, the
      // first in the line or just part of a next line.
      if (chunks[cur_chunk_in_line].first == TokenType::kNewLine) {
        if (cur_chunk_in_line != first_chunk_in_line) {
          current_length = adjusted_width + 1;
        } else {
          first_chunk_in_line++;
          cur_chunk_in_line++;
        }
      }

      if (trim_lines) {
        // Skip all white space chunks at start as they will be trimmed later
        while (cur_chunk_in_line < num_chunks
          && (chunks[cur_chunk_in_line].first == TokenType::kWhiteSpace)) {
          cur_chunk_in_line++;
          first_chunk_in_line++;
        }
      }
      // Keep on adding words in current line by iterating from starting word up
      // to last word in arr.
      while (cur_chunk_in_line < num_chunks) {

        // Update number of characters in current line. The new line token can
        // either have a size that will break the maximum width or a size of
        // zero depending on whether it is the first chunk, the first in the
        // line or just part of a next line.
        if (chunks[cur_chunk_in_line].first == TokenType::kNewLine) {
          if (cur_chunk_in_line != first_chunk_in_line) {
            current_length = adjusted_width + 1;
          }
        } else {
          if (ignore_ansi_escape_codes) {
            current_length
              += StripAnsiEscapeCodes(chunks[cur_chunk_in_line].second).size();
          } else {
            current_length += chunks[cur_chunk_in_line].second.size();
          }
        }

        // If limit of characters is violated then no more words can be added to
        // current line, unless what we are adding is white space, and we've
        // been configured to trim white space.
        if (current_length > adjusted_width) {
          if (chunks[cur_chunk_in_line].first == TokenType::kWhiteSpace
            && trim_lines) {
            // Will be trimmed later so don't count it
            if (ignore_ansi_escape_codes) {
              current_length
                -= StripAnsiEscapeCodes(chunks[cur_chunk_in_line].second)
                     .size();
            } else {
              current_length -= chunks[cur_chunk_in_line].second.size();
            }
          }
          // Abort adding the current chunk to the current line, unless it is
          // the only chunk in the line. In that case, we accept it even if it's
          // going to make the line longer than the maximum adjusted width.
          else if (cur_chunk_in_line > first_chunk_in_line) {
            break;
          }
        }

        // If current chunk being added to the line is the last chunk then
        // current line is the last line. Cost of last line is 0. Else cost is
        // square of extra spaces plus cost of putting line breaks in rest of
        // words from j+1 to n-1.
        if (cur_chunk_in_line == num_chunks - 1) {
          cost = 0;
        } else {
          cost = (adjusted_width - current_length)
              * (adjusted_width - current_length)
            + costs[cur_chunk_in_line + 1];
        }

        // Check if this arrangement gives minimum cost for line starting with
        // word arr[index_i].
        if (cost < costs[cur_chunk]) {
          costs[cur_chunk] = cost;
          optimized[cur_chunk] = cur_chunk_in_line;
        }

        // Do not pre-maturely break out if the current line exceeds the maximum
        // width, allowing to handle the edge case when a single token is longer
        // than the line width.

        cur_chunk_in_line++;
      }

      // If we reach the end, having skipped white spaces all the way, then this
      // is a line that can take all available chunks starting from cur_chunk.
      if (cur_chunk_in_line == num_chunks
        && first_chunk_in_line == cur_chunk_in_line) {
        costs[cur_chunk] = 0;
        optimized[cur_chunk] = num_chunks - 1;
      }
    } while (cur_chunk > 0);
  }

  // Print starting index and ending index of words present in each line.
  std::vector<std::string> result;
  cur_chunk = 0;
  auto first_line { true };
  while (cur_chunk < num_chunks) {
    // std::cout << cur_chunk + 1 << " " << optimized[cur_chunk] + 1 << " : ";
    std::string line = cur_chunk == 0 ? initial_indent : indent;
    size_t start = cur_chunk;
    size_t end = optimized[cur_chunk] + 1;
    // Always trim new lines (eventually creating an empty line if needed), and
    // if TrimLines is true, then also trim whitespaces
    while (start < end) {
      if (chunks[start].first == TokenType::kNewLine) {
        if (first_line) {
          // Add an empty line and continue
          result.emplace_back(line);
          line = indent;
        }
        start++;
        if (start == end) {
          break;
        }
      } else if (trim_lines
        && (chunks[start].first == TokenType::kWhiteSpace)) {
        start++;
        if (start == end) {
          // Add an empty line and continue
          result.emplace_back(line);
          line = indent;
        }
      } else {
        break;
      }
    }
    while ((end - 1) > start) {
      if ((trim_lines && (chunks[end - 1].first == TokenType::kWhiteSpace))) {
        end--;
        if (start == end) {
          // Add an empty line and continue
          result.emplace_back(line);
          line = indent;
        }
      } else {
        break;
      }
    }
    if (end > start) {
      for (cur_chunk_in_line = start; cur_chunk_in_line < end;
        cur_chunk_in_line++) {
        // std::cout << chunks[cur_chunk_in_line].second;
        line.append(chunks[cur_chunk_in_line].second);
      }
      result.push_back(std::move(line));
      // std::cout << std::endl;
    }
    cur_chunk = optimized[cur_chunk] + 1;
    first_line = false;
  }

  return result;
}

auto MoveAppend(std::vector<std::string> src, std::vector<std::string>& dst)
  -> void
{
  if (dst.empty()) {
    dst = std::move(src);
  } else {
    dst.reserve(dst.size() + src.size());
    std::ranges::move(src, std::back_inserter(dst));
    src.clear();
  }
}
} // namespace

/*!
  Uses a dynamic programming algorithm to wrap text so each line is at most the
  configured width, minimizing raggedness by penalizing extra spaces.

  Handles multiple paragraphs (separated by empty lines), indentation, tab
  expansion, whitespace collapsing, optional hyphen-based word breaking, and
  (optionally) ignoring ANSI escape codes in width calculation.

  If IgnoreAnsiEscapeCodes is enabled, only CSI (Control Sequence Introducer)
  ANSI codes matching the regex "\\x1B\\[[0-9;]*[A-Za-z]" are ignored for width
  calculation, but preserved in output. This covers SGR and most color/
  formatting codes, but not OSC, DCS, or other non-CSI codes.

  @param str Input string, may contain multiple paragraphs separated by empty
  lines.
  @return Optional vector of output lines (no trailing newlines). Returns
  `std::nullopt` if input is empty or tokenization fails.

  ### Performance Characteristics

  - Time Complexity: O(n^2) for cost table, O(n) for result assembly.
  - Memory: O(n)
  - Optimization: Minimizes raggedness; handles edge cases for long words.

  ### Usage Examples

  ```cpp
  std::string input = "\x1B[31mHello\x1B[0m world.";
  auto wrapper = oxygen::wrap::MakeWrapper().Width(5).IgnoreAnsiEscapeCodes();
  auto lines = wrapper.Wrap(input);
  // Output: {"\x1B[31mHello\x1B[0m", "world."}
  ```

  @note Paragraphs are separated by empty lines in the output.
  @see Fill, TextWrapperBuilder, TextWrap.cpp
*/
auto oxygen::wrap::TextWrapper::Wrap(const std::string& str) const
  -> std::optional<std::vector<std::string>>
{
  const auto tokenizer = Tokenizer(tab_, collapse_ws_, break_on_hyphens_);

  std::vector<std::string> result;
  std::vector<Token> chunks;
  const TokenConsumer consume_token =
    [&chunks, this, &result](TokenType token_type, std::string token) -> void {
    if ((token_type == TokenType::kParagraphMark
          || token_type == TokenType::kEndOfInput)
      && !chunks.empty()) {
      if (!result.empty()) {
        result.emplace_back("");
      }
      MoveAppend(WrapChunks(chunks, width_, indent_, initial_indent_,
                   trim_lines_, ignore_ansi_escape_codes_),
        result);
      chunks.clear();
    } else {
      chunks.emplace_back(token_type, std::move(token));
    }
  };

  if (tokenizer.Tokenize(str, consume_token)) {
    return std::make_optional(result);
  }

  return {};
}

/*!
  Wraps text so every line is at most the TextWrapper's `width` characters long
  and returns a single string containing the result. Equivalent to joining the
  result of `Wrap()` using `\n`.

  Returns std::nullopt if input is empty or tokenization fails.

  If IgnoreAnsiEscapeCodes is enabled, only CSI ANSI codes matching the regex
  "\\x1B\\[[0-9;]*[A-Za-z]" are ignored for width calculation, but preserved
  in output. See Wrap() for details.

  @param str Input string, may contain multiple paragraphs separated by empty
  lines.
  @return A single string containing the result, lines separated by `\n`.
  Returns `std::nullopt` on error.

  ### Performance Characteristics

  - Time Complexity: O(n^2) for wrapping, O(n) for joining.
  - Memory: O(n)

  ### Usage Examples

  ```cpp
  std::string input = "\x1B[31mHello\x1B[0m world.";
  auto wrapper = oxygen::wrap::MakeWrapper().Width(5).IgnoreAnsiEscapeCodes();
  auto filled = wrapper.Fill(input);
  // Output: "\x1B[31mHello\x1B[0m\nworld."
  ```

  @see Wrap, TextWrapperBuilder, TextWrap.cpp
*/
auto oxygen::wrap::TextWrapper::Fill(const std::string& str) const
  -> std::optional<std::string>
{

  const auto wrap_opt = Wrap(str);
  if (!wrap_opt) {
    return {};
  }
  const auto& wrap = wrap_opt.value();
  std::string result;
  auto size = std::accumulate(wrap.cbegin(), wrap.cend(),
    static_cast<size_t>(0), [](const size_t acc, const std::string& line) {
      return acc + line.length() + 1;
    });
  if (size > 0) {
    size -= 1; // remove the end of line from the last line
  }
  result.resize(size);
  [[maybe_unused]] auto acc = std::accumulate(wrap.cbegin(), wrap.cend(),
    result.begin(), [&result](const auto& dest, const std::string& line) {
      auto next_write = std::copy(line.cbegin(), line.cend(), dest);
      // Do not add a '\n' at the end of the string
      if (next_write != result.end()) {
        *next_write = '\n';
        ++next_write;
      }
      return next_write;
    });
  return std::make_optional(result);
}

/*!
  Sets the maximum line length for wrapped output. If any word in the input
  exceeds this width, the line may be longer to accommodate it.

  @param width Maximum line length.
  @return Reference to this builder for chaining.
  @see TextWrapper
  @note Default is 80.
*/
auto oxygen::wrap::TextWrapperBuilder::Width(const size_t width)
  -> TextWrapperBuilder&
{
  wrapper.width_ = width;
  return *this;
}

/*!
  Enables ignoring ANSI escape codes in width calculation. When enabled, only
  CSI (Control Sequence Introducer) ANSI codes matching the regex
  "\\x1B\\[[0-9;]*[A-Za-z]" are ignored for width calculation, but preserved
  in output. This covers SGR and most color/formatting codes, but not OSC, DCS,
  or other non-CSI codes.

  @return Reference to this builder for chaining.
  @note Default is false.
  @see StripAnsiEscapeCodes, TextWrapper, Wrap
*/
auto oxygen::wrap::TextWrapperBuilder::IgnoreAnsiEscapeCodes()
  -> TextWrapperBuilder&
{
  wrapper.ignore_ansi_escape_codes_ = true;
  return *this;
}

/*!
  Begins configuration of indentation for wrapped output. Use Initially() and
  Then() to set first-line and subsequent-line prefixes.

  @return Reference to this builder for chaining.
  @see Initially, Then
*/
auto oxygen::wrap::TextWrapperBuilder::IndentWith() -> TextWrapperBuilder&
{
  return *this;
}

/*!
  Sets the string prepended to the first line of wrapped output. Counts towards
  the length of the first line. The empty string is not indented.

  @param initial_indent String to prepend to the first line.
  @return Reference to this builder for chaining.
  @see IndentWith, Then
  @note Default is empty string.
*/
auto oxygen::wrap::TextWrapperBuilder::Initially(std::string initial_indent)
  -> TextWrapperBuilder&
{
  wrapper.initial_indent_ = std::move(initial_indent);
  return *this;
}

/*!
  Sets the string prepended to all lines except the first. Counts towards the
  length of each line. The empty string is not indented.

  @param indent String to prepend to subsequent lines.
  @return Reference to this builder for chaining.
  @see IndentWith, Initially
  @note Default is empty string.
*/
auto oxygen::wrap::TextWrapperBuilder::Then(std::string indent)
  -> TextWrapperBuilder&
{
  wrapper.indent_ = std::move(indent);
  return *this;
}

/*!
  Sets the string used to expand tab characters in input text. This is the first
  transformation before whitespace collapsing and wrapping.

  @param tab String to use for tab expansion.
  @return Reference to this builder for chaining.
  @note Default is "\t".
*/
auto oxygen::wrap::TextWrapperBuilder::ExpandTabs(std::string tab)
  -> TextWrapperBuilder&
{
  wrapper.tab_ = std::move(tab);
  return *this;
}

/*!
  Enables collapsing contiguous whitespace into a single space after tab
  expansion. Useful for normalizing input text.

  @return Reference to this builder for chaining.
  @note White space collapsing is done after tab expansion; if tab expansion
  uses non-whitespace, collapsing may be superseded.
  @note Default is false.
*/
auto oxygen::wrap::TextWrapperBuilder::CollapseWhiteSpace()
  -> TextWrapperBuilder&
{
  wrapper.collapse_ws_ = true;
  return *this;
}

/*!
  Enables trimming whitespace at the beginning and end of every line after
  wrapping but before indenting.

  @return Reference to this builder for chaining.
  @note Default is false.
*/
auto oxygen::wrap::TextWrapperBuilder::TrimLines() -> TextWrapperBuilder&
{
  wrapper.trim_lines_ = true;
  return *this;
}

/*!
  Enables breaking compound words into separate chunks right after hyphens, as
  is customary in English.

  @return Reference to this builder for chaining.
  @note Default is false.
*/
auto oxygen::wrap::TextWrapperBuilder::BreakOnHyphens() -> TextWrapperBuilder&
{
  wrapper.break_on_hyphens_ = true;
  return *this;
}

/*!
  Produces a string representation of the configuration parameters of a
  TextWrapper instance for debugging purposes.

  @param wrapper The TextWrapper to convert.
  @return String representation of the configuration.

  ### Usage Example

  ```cpp
  std::string config = wrapper.to_string();
  // config: "{w:80,t:'\t',tl:0,boh:0}"
  ```
*/
auto TextWrapper::to_string() const -> std::string
{
  std::string result = "{w:";
  result += std::to_string(width_);
  result += ",t:'";
  result += tab_;
  result += "',tl:";
  result += (trim_lines_ ? "1" : "0");
  result += ",boh:";
  result += (break_on_hyphens_ ? "1" : "0");
  result += ",ansi:";
  result += (ignore_ansi_escape_codes_ ? "1" : "0");
  result += "}";
  return result;
}

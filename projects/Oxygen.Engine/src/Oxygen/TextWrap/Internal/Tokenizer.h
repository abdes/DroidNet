//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <iostream>

#include <Oxygen/TextWrap/api_export.h>

/// Internal implementation details for the text wrapper.
namespace oxygen::wrap::internal {

/*!
 * \brief Different types of tokens that can be produced by this Tokenizer.
 */
enum class TokenType : uint8_t {
  /// A chunk of text with no white space in it.
  kChunk,
  /// A series of white spaces (`<SPACE>`, `\\t`, `\\f`, `\\r`)
  kWhiteSpace,
  /// New line is marked by either `\\n` or `\\v` while `\\r` and `\\f` are
  /// always replaced with a single space ` `.
  kNewLine,
  /// Marks a paragraph boundary (two consecutive new lines `\n\n`).
  kParagraphMark,
  /// The last token emitted by the tokenizer marking the end of input.
  kEndOfInput
};

//! String representation of enum values in `Format`.
OXGN_TXW_NDAPI auto to_string(TokenType value) -> const char*;

/*!
 * \brief Overloaded output stream operator for pretty printing TokenType
 * values.
 *
 * @param out output stream to which formatted data will be inserted.
 * @param token_type the TokenType value to write to the stream.
 * @return out
 */
OXGN_TXW_API auto operator<<(std::ostream& out, const TokenType& token_type)
  -> std::ostream&;

/*!
 * \brief A token is a pair of TokenType and a string representing the token
 * value.
 *
 * The EndOfInput token type will always have an empty string ("") as a value.
 */
using Token = std::pair<TokenType, std::string>;

/*!
 * \brief A callback that will be called each time a token is ready to be
 * consumed.
 *
 * **Example**
 *
 * \snippet tokenizer_test.cpp Example token consumer
 */
using TokenConsumer
  = std::function<void(TokenType token_type, std::string token)>;

/*!
 * \brief Transform a text formed of lines and paragraphs into a stream of typed
 * tokens for further processing by a token consumer.
 *
 * To make text processing and formatting simpler, the algorithms work on
 * indivisible chunks of text separated by white spaces and eventually paragraph
 * markers.
 *
 * Chunks are not the same as words; for example when word breaking on hyphens
 * is activated, a hyphenated word would be broken into multiple chunks just
 * where the hyphens are located.
 *
 * Chunks will never have white spaces in them. Contiguous white spaces are
 * concatenated into a single block and presented as a single token. A special
 * case of white space is when two consecutive `\n` characters are encountered.
 * This is considered a paragraph marker and presented as a specific token:
 * `TokenType::ParagraphMark`.
 *
 * As an example, the text:
 *
 *   "Just plain finger-licking good!"
 *
 * breaks into the following chunks:
 *
 *   'Just', ' ', 'plain', ' ', 'finger-', 'licking', ' ', 'good!'
 *
 * if `break_on_hyphens` is `true`; or in :
 *
 *   'Just', ' ', 'plain', ' ', 'finger-licking', ' ', 'good!'
 *
 * otherwise.
 *
 * In addition to breaking text into chunks, the Tokenizer is also responsible
 * for implementing two specific behaviors prior to the text wrapping/
 * formatting, and which can be controlled by configuration parameters passed to
 * the Tokenizer constructor:
 *
 * 1. **Tab expansion**:
 *
 *    controlled with the `tab` configuration parameter. All tab characters in
 *    the text will be replaced with the content of `tab`. For example, to
 *    expand tabs to spaces, one would specify the `tab` value to be as many
 *    spaces as a tab character should expand to. To keep tabs as they are,
 *    simply specify a `tab` value of `"\t"`.
 *
 * 2. **Special characters**:
 *
 *    The special characters '\r' and '\f' are always ignored as they do not add
 *    value to the proper formatting and wrapping of the text.
 *
 *    Both '\n' and '\v' are considering as line breaks.
 *
 * 3. **Collapse white space**:
 *
 *    controlled with the `collapse_ws` configuration parameter. If `true`, a
 *    contiguous series of white space characters will be replaced with a single
 *    `<SPACE>`.
 *
 * 4. **Break on hyphens**:
 *
 *    controlled with the `break_on_hyphens` configuration parameter. If `true`,
 *    compound words will be broken into separate chunks right after hyphens, as
 *    it is customary in English. If `false`, only white spaces will be
 *    considered as chunk boundaries.
 *
 * **Example**
 *
 * \snippet tokenizer_test.cpp Tokenizer example
 */
class Tokenizer {
public:
  /*!
   * \brief Create a new instance of the Tokenizer class configured with the
   * given parameters.
   *
   * \see Tokenizer class documentation for a detailed description of all
   * configuration parameters and the associated behaviors.
   *
   * @param tab string to which tab character should be expanded.
   * @param collapse_ws controls collapsing of multiple white spaces into a
   * single space.
   * @param break_on_hyphens controls whether hyphens can be used to break words
   * into multiple chunks.
   */
  explicit Tokenizer(std::string tab, bool collapse_ws, bool break_on_hyphens)
    : tab_(std::move(tab))
    , collapse_ws_ { collapse_ws }
    , break_on_hyphens_ { break_on_hyphens }
  {
  }

  /*!
   * \brief Transform the given text into a stream of tokens.
   *
   * Tokens produced by the Tokenizer are consumed via the TokenConsumer passed
   * as an argument to this method.
   * @param text the input text to be split into tokens.
   * @param consume_token the token consumer which will be called each time a
   * token is produced.
   * @return \b true if the tokenization completed successfully; \b false
   * otherwise.
   */
  [[nodiscard]] OXGN_TXW_API auto Tokenize(
    const std::string& text, const TokenConsumer& consume_token) const -> bool;

private:
  std::string tab_;
  bool collapse_ws_;
  bool break_on_hyphens_;
};

} // namespace oxygen::wrap::internal

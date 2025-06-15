//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::detail::query {

//! Detailed error information for path parsing failures.
/*!
 Contains comprehensive error reporting including position, message, and
 optional help text for diagnosing and fixing path syntax errors.
*/
struct PathErrorInfo {
  //! Human-readable error description
  std::string error_message;
  //! Character position in original string where error occurred
  size_t error_position = 0;
  //! Optional help message for fixing the error
  std::optional<std::string> error_help;
  //! Default constructor for creating empty error info.
  PathErrorInfo() = default;

  //! Constructs error info with message, position, and optional help.
  /*!
   @param message Human-readable error description
   @param position Character position where error occurred (0-based)
   @param help Optional help text for fixing the error
  */
  explicit PathErrorInfo(std::string message, size_t position = 0,
    std::optional<std::string> help = std::nullopt)
    : error_message(std::move(message))
    , error_position(position)
    , error_help(std::move(help))
  {
  }
  //! Default destructor.
  ~PathErrorInfo() noexcept = default;

  // Rule of 5 compliance using OXYGEN macros
  OXYGEN_DEFAULT_COPYABLE(PathErrorInfo)
  OXYGEN_DEFAULT_MOVABLE(PathErrorInfo)
};

//! Represents a single segment in a hierarchical path pattern.
/*!
 Path segments contain the name/pattern and metadata about wildcard usage.
 Supports literal names, single-level wildcards (*), and recursive
 wildcards (**) with position tracking for error reporting.
*/
struct PathSegment {
  std::string name; //!< Segment name or wildcard pattern
  //! Absolute position in original string where this segment starts
  size_t start_position { 0 };
  bool is_wildcard_single { false }; //!< True if this segment is * wildcard
  bool is_wildcard_recursive { false }; //!< True if this segment is ** wildcard

  //! Constructs a path segment with the given name.
  /*!
   @param segment_name Name of the path segment (may be empty)
   @param position Starting position in original path string
   @param is_single_wildcard True if this segment represents a single wildcard
   (*)
   @param is_recursive_wildcard True if this segment represents recursive
   wildcard (**)
  */
  explicit PathSegment(std::string segment_name, size_t position = 0,
    bool is_single_wildcard = false, bool is_recursive_wildcard = false)
    : name(std::move(segment_name))
    , start_position(position)
    , is_wildcard_single(is_single_wildcard)
    , is_wildcard_recursive(is_recursive_wildcard)
  {
  }
  //! Default destructor.
  ~PathSegment() noexcept = default;

  // Rule of 5 compliance using OXYGEN macros
  OXYGEN_DEFAULT_COPYABLE(PathSegment)
  OXYGEN_DEFAULT_MOVABLE(PathSegment)

  //! Equality comparison operator.
  /*!
   @param other PathSegment to compare against
   @return True if all fields are equal
  */
  OXGN_SCN_NDAPI auto operator==(const PathSegment& other) const noexcept
    -> bool;

  //! Inequality comparison operator.
  /*!
   @param other PathSegment to compare against
   @return True if any fields differ
  */
  OXGN_SCN_NDAPI auto operator!=(const PathSegment& other) const noexcept
    -> bool;
};

//! Result of parsing a hierarchical path pattern.
/*!
 Contains the parsed segments, original path string, metadata about wildcard
 usage, and detailed error information if parsing failed. Provides convenient
 methods for validity checking and introspection.
*/
struct ParsedPath {
  std::vector<PathSegment> segments; //!< Parsed path segments in order
  std::string original_path; //!< Original unparsed path string
  bool has_wildcards { false }; //!< True if any segment contains wildcards
  std::optional<PathErrorInfo>
    error_info {}; //!< Error details if parsing failed

  // Convenience methods
  //! Checks if parsing was successful.
  /*!
   @return True if no parsing errors occurred
  */
  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return !error_info.has_value();
  }

  //! Checks if the parsed path contains no segments.
  /*!
   @return True if segments vector is empty
  */
  [[nodiscard]] auto IsEmpty() const noexcept -> bool
  {
    return segments.empty();
  }

  //! Returns the number of parsed segments.
  /*!
   @return Count of segments in the parsed path
  */
  [[nodiscard]] auto Size() const noexcept -> size_t { return segments.size(); }
};

/// Parser for hierarchical path patterns with wildcard support and intelligent
/// optimization.
///
/// Provides robust parsing of path strings into structured PathSegment
/// collections with comprehensive error handling, escape sequence processing,
/// and wildcard normalization. The parser implements sophisticated wildcard
/// optimization rules to minimize redundant pattern matching operations.
///
/// ### Supported Path Syntax
///
/// - **Literal segments**: `Player`, `Equipment`, `Weapon`
/// - **Single-level wildcards**: `*` matches any direct child name
/// - **Recursive wildcards**: `**` matches any sequence of nodes at any depth
/// - **Escape sequences**: `\*`, `\**`, `\\`, `\/` for literal characters
/// - **Path separators**: `/` delimits hierarchy levels
/// - **UTF-8 support**: Full Unicode character support in segment names
///
/// ### Wildcard Optimization Rules
///
/// The parser automatically applies optimization rules to reduce redundant
/// patterns:
/// - **Rule 1**: `*/**` → `**` (single followed by recursive becomes recursive)
/// - **Rule 2**: `**/*` → `**` (recursive absorbs following single wildcards)
/// - **Consecutive recursive**: Multiple `**` patterns are collapsed to single
/// `**`
///
/// ### Error Handling
///
/// Comprehensive error reporting with precise position tracking:
/// - Invalid characters (control chars, non-UTF-8 sequences)
/// - Malformed escape sequences (`\x`, `\z`, etc.)
/// - Unterminated escape sequences (trailing backslash)
/// - Position-accurate error messages with help text
///
/// ### Performance Characteristics
///
/// - **Time Complexity**: O(n) where n is path string length
/// - **Memory**: O(k) where k is number of path segments
/// - **UTF-8 Processing**: Single-pass validation and normalization
/// - **Error Recovery**: Fails fast on first syntax error
///
/// ### Usage Examples
///
/// ```cpp
/// // Basic path parsing
/// PathParser parser("World/Player/Equipment");
/// auto result = parser.Parse();
/// if (result.IsValid()) {
///   // Process result.segments
/// }
///
/// // Wildcard patterns with optimization
/// PathParser wildcard_parser("Level/**/Enemy");
/// auto wildcard_result = wildcard_parser.Parse();
/// // Optimizes "*/**" patterns automatically
///
/// // Escape sequences for literal characters
/// PathParser escape_parser(R"(Config/\*/Settings)");
/// auto escape_result = escape_parser.Parse();
/// // Matches literal "*" character in path
///
/// // Error handling with detailed diagnostics
/// PathParser error_parser("Invalid\x01Path");
/// auto error_result = error_parser.Parse();
/// if (!error_result.IsValid()) {
///   // error_result.error_info contains position and help text
/// }
/// ```
///
/// @note The parser is stateless after construction; multiple Parse() calls
/// return the same result without re-parsing.
/// @note Path strings are stored as string_view; caller must ensure lifetime
/// during parsing.
/// @warning Empty segments (from consecutive slashes) generate debug warnings
/// but are preserved.
///
/// @see ParsedPath for complete parsing results and validation methods
/// @see PathSegment for individual segment structure and wildcard metadata
/// @see PathErrorInfo for detailed error reporting with position tracking
class PathParser {
public:
  //! Constructs parser for the given path string.
  /*!
   @param path Path pattern string to parse
   @note Path is stored as string_view; caller must ensure lifetime
  */
  OXGN_SCN_API explicit PathParser(std::string_view path);

  //! Default destructor.
  ~PathParser() noexcept = default;

  // Rule of 5 compliance using OXYGEN macros
  OXYGEN_DEFAULT_COPYABLE(PathParser)
  OXYGEN_DEFAULT_MOVABLE(PathParser)

  //! Parses the path string into structured segments.
  /*!
   @return ParsedPath containing segments or error information
   @note Multiple calls return the same result; parser is stateless after
   construction
  */
  OXGN_SCN_NDAPI auto Parse() -> ParsedPath;

private:
  //! Original path string being parsed
  std::string_view original_path_;
  //! Current parsing position in string
  size_t current_position_;
  //! Tracks consecutive ** wildcards
  bool last_segment_was_recursive_wildcard_;
  //! Accumulated parsing results
  ParsedPath result_;
  //! Flag to track if parsing has been done
  mutable bool parsing_completed_;

  [[nodiscard]] auto IsAtEnd() const noexcept -> bool;

  [[nodiscard]] auto PeekChar() const noexcept -> char;

  auto ConsumeChar() noexcept -> char;

  [[nodiscard]] static auto IsValidPathCharacter(char c) noexcept -> bool;

  auto ExtractAndNormalizeSegment() -> std::string;

  auto ProcessEscapeSequence(std::string& segment) -> bool;

  [[nodiscard]] auto PeekNextSegment() const -> std::optional<std::string>;

  void ProcessSegment(
    const std::string& segment_name, size_t segment_start_position);

  void AddSegment(const std::string& name, size_t start_position,
    bool is_recursive, bool is_single);

  void ReportInvalidCharacter(char invalid_char);

  void ReportInvalidEscapeSequence();

  void ReportUnterminatedEscape();
};

//! Reconstructs a normalized path string from parsed segments.
/*!
 Parses the input path and reconstructs it as a clean string representation,
 effectively removing redundant wildcards.

 @param path Input path string to normalize
 @return Normalized path string with optimized wildcard patterns, or original
 string if parsing fails
 @see PathParser for supported syntax and optimization rules
*/
OXGN_SCN_NDAPI auto NormalizePath(std::string_view path) -> std::string;

//! Convenience wrapper for single-use path parsing operations.
/*!
 Creates a temporary PathParser instance and returns the parsing result.
 Equivalent to `PathParser(path_string).Parse()` but more concise for
 one-time parsing operations.

 @param path_string Path pattern string to parse
 @return ParsedPath result containing segments or error information
 @see PathParser for detailed parsing control and reusable parser instances
*/
OXGN_SCN_NDAPI auto ParsePath(std::string_view path_string) -> ParsedPath;

} // namespace oxygen::scene::detail::query

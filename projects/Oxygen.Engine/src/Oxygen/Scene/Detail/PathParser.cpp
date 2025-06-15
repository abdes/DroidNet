//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Detail/PathParser.h>

#include <iomanip>
#include <sstream>

using oxygen::scene::detail::query::ParsedPath;

namespace {
void LogParseError(const ParsedPath& path)
{
  if (path.IsValid()) {
    // No error to log, or this function should only be called with invalid
    // paths.
    return;
  }

  // The LOG_SCOPE_F macro automatically handles the "{ PathParser Error" and "}
  // 0.000 s: PathParser Error" lines.
  LOG_SCOPE_F(ERROR, "PathParser Error");

  // Log the error message itself
  LOG_F(ERROR, "{}", path.error_info->error_message);

  // Log the original path string, quoted
  LOG_F(ERROR, "\"{}\"", path.original_path);

  // Log the position indicator and help message, if applicable
  if (path.error_info->error_position != std::string_view::npos
    && path.error_info->error_position < path.original_path.length()) {
    std::string indicator_line = "";
    // Adjust for the opening quote character '"' when calculating spaces for
    // the indicator
    indicator_line += std::string(path.error_info->error_position + 1, ' ');
    indicator_line += '^';
    if (path.error_info->error_help.has_value()) {
      indicator_line += " help: ";
      indicator_line += *path.error_info->error_help;
    }
    LOG_F(ERROR, "{}", indicator_line);
  } else if (path.error_info->error_help.has_value()) {
    // If there's no specific position, but there is a help message, log it.
    LOG_F(ERROR, "help: {}", *path.error_info->error_help);
  }
}
} // anonymous namespace

namespace oxygen::scene::detail::query {

// PathSegment operators
bool PathSegment::operator==(const PathSegment& other) const noexcept
{
  return name == other.name && start_position == other.start_position
    && is_wildcard_single == other.is_wildcard_single
    && is_wildcard_recursive == other.is_wildcard_recursive;
}

bool PathSegment::operator!=(const PathSegment& other) const noexcept
{
  return !(*this == other);
}

PathParser::PathParser(std::string_view path)
  : original_path_(path)
  , current_position_(0)
  , last_segment_was_recursive_wildcard_(false)
  , parsing_completed_(false)
{
  result_.original_path = path;
}

auto PathParser::Parse() -> ParsedPath
{
  // Return cached result if already parsed
  if (parsing_completed_) {
    return result_;
  }

  if (original_path_.empty()) {
    parsing_completed_ = true;
    return result_;
  }

  while (!IsAtEnd()) {
    // Extract segment (consume characters until '/' or end)
    size_t segment_start_position = current_position_;
    std::string segment_name = ExtractAndNormalizeSegment();

    if (result_.error_info.has_value()) {
      // Log the error and stop parsing
      LogParseError(result_);
      break;
    } // Process the segment with wildcard simplification
    ProcessSegment(segment_name, segment_start_position);

    // If we hit a '/', consume it
    bool had_slash = false;
    if (!IsAtEnd() && PeekChar() == '/') {
      ConsumeChar();
      had_slash = true;
    } // If the '/' was the last character, process one more empty segment
    if (had_slash && IsAtEnd()) {
      ProcessSegment("", current_position_);
    }
  }

  parsing_completed_ = true;
  return result_;
}

auto PathParser::IsAtEnd() const noexcept -> bool
{
  return current_position_ >= original_path_.size();
}

auto PathParser::PeekChar() const noexcept -> char
{
  return IsAtEnd() ? '\0' : original_path_[current_position_];
}

auto PathParser::ConsumeChar() noexcept -> char
{
  if (IsAtEnd()) {
    return '\0';
  }
  return original_path_[current_position_++];
}

auto PathParser::IsValidPathCharacter(char c) noexcept -> bool
{
  // For UTF-8 support: Allow all characters except control characters and '/'
  // (path separator) Control characters (0x00-0x1F and 0x7F-0x9F) are not
  // allowed UTF-8 continuation bytes (0x80-0xBF) and start bytes (0xC0-0xFF)
  // are allowed
  unsigned char uc = static_cast<unsigned char>(c);

  // Reject path separator
  if (c == '/') {
    return false;
  }

  // Reject ASCII control characters (0x00-0x1F and 0x7F)
  if (uc <= 0x1F || uc == 0x7F) {
    return false;
  }

  // Allow all other characters (including UTF-8 bytes)
  return true;
}

auto PathParser::ExtractAndNormalizeSegment() -> std::string
{
  std::string segment;

  while (!IsAtEnd() && PeekChar() != '/') {
    char c = PeekChar();

    if (c == '\\') {
      if (!ProcessEscapeSequence(segment)) {
        // Error already reported by ProcessEscapeSequence
        return segment;
      }
    } else if (IsValidPathCharacter(c)) {
      segment += ConsumeChar();
    } else {
      ReportInvalidCharacter(c);
      return segment;
    }
  }

  // Don't skip slashes here - let the main loop handle them
  return segment;
}

auto PathParser::ProcessEscapeSequence(std::string& segment) -> bool
{
  if (current_position_ + 1 >= original_path_.size()) {
    ReportUnterminatedEscape();
    return false;
  }

  ConsumeChar(); // Consume the backslash
  char next_char = PeekChar();

  if (next_char == '*' || next_char == '\\' || next_char == '/') {
    segment += '\\';
    segment += ConsumeChar();
    return true;
  }

  // Check for \** sequence
  if (next_char == '*' && current_position_ + 1 < original_path_.size()
    && original_path_[current_position_ + 1] == '*') {
    segment += "\\**";
    ConsumeChar(); // First *
    ConsumeChar(); // Second *
    return true;
  }

  ReportInvalidEscapeSequence();
  return false;
}

auto PathParser::PeekNextSegment() const -> std::optional<std::string>
{
  if (IsAtEnd()) {
    return std::nullopt;
  }

  size_t saved_position = current_position_;

  // Create a temporary parser state for lookahead
  PathParser temp_parser(original_path_);
  temp_parser.current_position_ = saved_position;

  std::string next_segment = temp_parser.ExtractAndNormalizeSegment();

  if (temp_parser.result_.error_info.has_value()) {
    return std::nullopt; // Next segment is invalid
  }

  return next_segment;
}

void PathParser::ProcessSegment(
  const std::string& segment_name, size_t segment_start_position)
{
  // Log warning for empty segments in debug builds
  if (segment_name.empty()) {
    DLOG_F(WARNING, "Empty path segment found at position {} in path '{}'",
      segment_start_position, original_path_);
  }

  // Apply wildcard simplification rules by looking at the previous segment:
  // Rule 1: */** → ** (single followed by recursive becomes just recursive)
  // Rule 2: **/* → ** (recursive absorbs following single)

  if (segment_name == "**") {
    // Rule 1: If the last segment was "*", apply */** → **
    if (!result_.segments.empty() && result_.segments.back().name == "*"
      && result_.segments.back().is_wildcard_single) {
      // Remove the last "*" segment, we'll add "**" with the position of the
      // "*"
      size_t star_position = result_.segments.back().start_position;
      result_.segments.pop_back();
      AddSegment("**", star_position, true, false);
    } else if (!last_segment_was_recursive_wildcard_) {
      // Add "**" if we don't already have one
      AddSegment("**", segment_start_position, true, false);
    }
    // Else: skip this redundant "**"

    last_segment_was_recursive_wildcard_ = true;
    result_.has_wildcards = true;

  } else if (segment_name == "*") {
    // Rule 2: **/* → ** (recursive absorbs following single)
    if (last_segment_was_recursive_wildcard_) {
      // Skip this "*" - it's absorbed by the previous "**"
    } else {
      AddSegment("*", segment_start_position, false, true);
      last_segment_was_recursive_wildcard_ = false;
      result_.has_wildcards = true;
    }

  } else {
    // Regular segment (including empty segments)
    bool is_recursive = false;
    bool is_single = false;
    AddSegment(segment_name, segment_start_position, is_recursive, is_single);
    last_segment_was_recursive_wildcard_ = false;
  }
}

void PathParser::AddSegment(const std::string& name, size_t start_position,
  bool is_recursive, bool is_single)
{
  result_.segments.emplace_back(name, start_position, is_single, is_recursive);
}

void PathParser::ReportInvalidCharacter(char invalid_char)
{
  // Format character for display - show hex code for non-printable chars
  std::string char_display;
  if (invalid_char >= 32 && invalid_char <= 126) {
    // Printable ASCII character
    char_display = "'" + std::string(1, invalid_char) + "'";
  } else {
    // Non-printable character - show hex code
    std::ostringstream oss;
    oss << "'\\x" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(2) << static_cast<unsigned char>(invalid_char) << "'";
    char_display = oss.str();
  }

  result_.error_info
    = PathErrorInfo { "Invalid character " + char_display + " in path segment",
        current_position_, "Remove or escape the invalid character" };
}

void PathParser::ReportInvalidEscapeSequence()
{
  result_.error_info = PathErrorInfo { "Invalid escape sequence",
    current_position_ - 1, R"(Use \*, \**, \\, or \/)" };
}

void PathParser::ReportUnterminatedEscape()
{
  result_.error_info
    = PathErrorInfo { "Unterminated escape sequence at end of path",
        current_position_,
        "Complete the escape sequence or remove the trailing backslash" };
}

} // namespace oxygen::scene::detail::query

auto oxygen::scene::detail::query::ParsePath(std::string_view path_string)
  -> ParsedPath
{
  PathParser parser(path_string);
  return parser.Parse();
}

auto oxygen::scene::detail::query::NormalizePath(std::string_view path)
  -> std::string
{
  // Parse the path and when successful, returns a re-constructed string
  // representation of the ParsedPath segments.
  ParsedPath parsed = ParsePath(path);
  if (!parsed.IsValid()) {
    return "";
  }

  // Reconstruct the normalized path from the parsed segments
  std::string normalized_path;
  for (const auto& segment : parsed.segments) {
    if (!normalized_path.empty()) {
      normalized_path += '/';
    }
    normalized_path += segment.name;
  }

  return normalized_path;
}

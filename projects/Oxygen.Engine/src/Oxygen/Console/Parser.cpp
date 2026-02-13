//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>

#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/Parser.h>

namespace oxygen::console {

namespace {

  auto TrimWhitespace(const std::string_view value) -> std::string_view
  {
    size_t begin = 0;
    while (begin < value.size()
      && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
      ++begin;
    }

    size_t end = value.size();
    while (end > begin
      && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
      --end;
    }

    return value.substr(begin, end - begin);
  }

  auto IsEscapableCharacter(const char c) -> bool
  {
    return c == '\\' || c == '"' || c == '\''
      || std::isspace(static_cast<unsigned char>(c)) != 0
      || c == kCommandChainSeparator;
  }

} // namespace

auto Parser::Tokenize(const std::string_view line) -> std::vector<std::string>
{
  std::vector<std::string> tokens;
  std::string current;

  bool in_quotes = false;
  char quote_char = '\0';
  bool escape_next = false;

  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (escape_next) {
      current.push_back(c);
      escape_next = false;
      continue;
    }

    if (c == '\\') {
      const bool has_next = (i + 1) < line.size();
      if (has_next && IsEscapableCharacter(line[i + 1])) {
        escape_next = true;
      } else {
        current.push_back(c);
      }
      continue;
    }

    if (in_quotes) {
      if (c == quote_char) {
        in_quotes = false;
        quote_char = '\0';
      } else {
        current.push_back(c);
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      in_quotes = true;
      quote_char = c;
      continue;
    }

    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }

    current.push_back(c);
  }

  if (!current.empty()) {
    tokens.push_back(std::move(current));
  }

  return tokens;
}

auto Parser::SplitCommands(const std::string_view line)
  -> std::vector<std::string>
{
  std::vector<std::string> commands;

  bool in_quotes = false;
  char quote_char = '\0';
  bool escape_next = false;
  size_t segment_start = 0;

  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];

    if (escape_next) {
      escape_next = false;
      continue;
    }

    if (c == '\\') {
      const bool has_next = (i + 1) < line.size();
      if (has_next && IsEscapableCharacter(line[i + 1])) {
        escape_next = true;
      }
      continue;
    }

    if (in_quotes) {
      if (c == quote_char) {
        in_quotes = false;
        quote_char = '\0';
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      in_quotes = true;
      quote_char = c;
      continue;
    }

    if (c == kCommandChainSeparator) {
      const auto segment
        = TrimWhitespace(line.substr(segment_start, i - segment_start));
      if (!segment.empty()) {
        commands.emplace_back(segment);
      }
      segment_start = i + 1;
    }
  }

  const auto last = TrimWhitespace(line.substr(segment_start));
  if (!last.empty()) {
    commands.emplace_back(last);
  }

  return commands;
}

} // namespace oxygen::console

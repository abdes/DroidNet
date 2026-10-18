//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <utf8proc.h>

#include <Oxygen/Content/Internal/Generated.UnicodeTables.h>
#include <Oxygen/Content/VirtualPath.h>

namespace oxygen::content {

namespace {

  namespace unicode = internal::unicode;

  //! Decodes strict UTF-8: no overlong forms, surrogates or code points past
  //! U+10FFFF.
  auto Decode(const std::string_view text) -> std::optional<std::u32string>
  {
    auto out = std::u32string {};
    out.reserve(text.size());
    auto index = std::size_t { 0 };
    while (index < text.size()) {
      const auto lead = static_cast<unsigned char>(text[index]);
      auto length = std::size_t { 0 };
      auto code = char32_t { 0 };
      auto minimum = char32_t { 0 };
      if (lead < 0x80U) {
        length = 1;
        code = lead;
      } else if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        code = lead & 0x1FU;
        minimum = 0x80;
      } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        code = lead & 0x0FU;
        minimum = 0x800;
      } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        code = lead & 0x07U;
        minimum = 0x10000;
      } else {
        return std::nullopt;
      }
      if (text.size() - index < length) {
        return std::nullopt;
      }
      for (auto offset = std::size_t { 1 }; offset < length; ++offset) {
        const auto next = static_cast<unsigned char>(text[index + offset]);
        if ((next & 0xC0U) != 0x80U) {
          return std::nullopt;
        }
        code = (code << 6U) | (next & 0x3FU);
      }
      if (code < minimum || code > 0x10FFFF
        || (code >= 0xD800 && code <= 0xDFFF)) {
        return std::nullopt;
      }
      out.push_back(code);
      index += length;
    }
    return out;
  }

  auto Encode(const std::u32string_view codes) -> std::string
  {
    auto out = std::string {};
    out.reserve(codes.size());
    for (const auto code : codes) {
      if (code < 0x80) {
        out.push_back(static_cast<char>(code));
      } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
      } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
      } else {
        out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
      }
    }
    return out;
  }

  auto IsControl(const char32_t code) -> bool
  {
    return code <= 0x1F || (code >= 0x7F && code <= 0x9F);
  }

  auto IsReserved(const char32_t code) -> bool
  {
    return std::u32string_view { U"\\/:*?\"<>|" }.contains(code);
  }

  auto IsAssigned(const char32_t code) -> bool
  {
    const auto it = std::ranges::upper_bound(unicode::kAssigned, code, {},
      [](const unicode::CodePointRange& range) -> char32_t {
        return range.first;
      });
    return it != unicode::kAssigned.begin() && code <= std::prev(it)->last;
  }

  auto Fold(const char32_t code) -> char32_t
  {
    const auto it = std::ranges::lower_bound(unicode::kSimpleCaseFolding, code,
      {}, [](const unicode::CaseFoldingPair& pair) -> char32_t {
        return pair.code;
      });
    return it != unicode::kSimpleCaseFolding.end() && it->code == code
      ? it->folded
      : code;
  }

  auto AsciiUpper(const char c) -> char
  {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
  }

  auto IsDeviceName(const std::string_view segment) -> bool
  {
    const auto stem = segment.substr(0, segment.find('.'));
    auto upper = std::string { stem };
    std::ranges::transform(upper, upper.begin(), AsciiUpper);
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") {
      return true;
    }
    return upper.size() == 4
      && (upper.starts_with("COM") || upper.starts_with("LPT"))
      && upper[3] >= '1' && upper[3] <= '9';
  }

  auto IsIdentifierStart(const char c) -> bool
  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
  }

  auto IsIdentifierChar(const char c) -> bool
  {
    return IsIdentifierStart(c) || (c >= '0' && c <= '9') || c == '-';
  }

  auto IsDerivedContent(const std::string_view segment) -> bool
  {
    return segment.starts_with('.') || segment == "SourceMedia";
  }

  auto Split(const std::string_view path) -> std::vector<std::string_view>
  {
    auto segments = std::vector<std::string_view> {};
    auto pos = std::size_t { 1 };
    while (true) {
      const auto next = path.find('/', pos);
      if (next == std::string_view::npos) {
        segments.push_back(path.substr(pos));
        return segments;
      }
      segments.push_back(path.substr(pos, next - pos));
      pos = next + 1;
    }
  }

} // namespace

auto to_string(const VirtualPathError error) noexcept -> const char*
{
  switch (error) {
  case VirtualPathError::kEmpty:
    return "vpath.empty";
  case VirtualPathError::kNotAbsolute:
    return "vpath.not_absolute";
  case VirtualPathError::kTrailingSlash:
    return "vpath.trailing_slash";
  case VirtualPathError::kEmptySegment:
    return "vpath.segment.empty";
  case VirtualPathError::kDotSegment:
    return "vpath.segment.dot";
  case VirtualPathError::kInvalidUtf8:
    return "vpath.segment.invalid_utf8";
  case VirtualPathError::kControlCharacter:
    return "vpath.segment.control_character";
  case VirtualPathError::kReservedCharacter:
    return "vpath.segment.reserved_character";
  case VirtualPathError::kUnassignedCodePoint:
    return "vpath.segment.unassigned_code_point";
  case VirtualPathError::kNotNfc:
    return "vpath.segment.not_nfc";
  case VirtualPathError::kEdgeSpace:
    return "vpath.segment.edge_space";
  case VirtualPathError::kTrailingDot:
    return "vpath.segment.trailing_dot";
  case VirtualPathError::kDeviceName:
    return "vpath.segment.device_name";
  case VirtualPathError::kSegmentTooLong:
    return "vpath.segment.too_long";
  case VirtualPathError::kInvalidMountName:
    return "vpath.mount.invalid";
  case VirtualPathError::kMountOnly:
    return "vpath.mount_only";
  case VirtualPathError::kMissingExtension:
    return "vpath.leaf.missing_extension";
  case VirtualPathError::kDerivedContent:
    return "vpath.segment.derived_content";
  }
  return "vpath.unknown";
}

auto ValidateVirtualPathSegment(const std::string_view segment)
  -> std::optional<VirtualPathError>
{
  if (segment.empty()) {
    return VirtualPathError::kEmptySegment;
  }
  if (segment == "." || segment == "..") {
    return VirtualPathError::kDotSegment;
  }
  const auto codes = Decode(segment);
  if (!codes.has_value()) {
    return VirtualPathError::kInvalidUtf8;
  }
  if (std::ranges::any_of(*codes, IsControl)) {
    return VirtualPathError::kControlCharacter;
  }
  if (std::ranges::any_of(*codes, IsReserved)) {
    return VirtualPathError::kReservedCharacter;
  }
  if (!std::ranges::all_of(*codes, IsAssigned)) {
    return VirtualPathError::kUnassignedCodePoint;
  }
  if (const auto nfc = ToNfc(segment); !nfc.has_value() || *nfc != segment) {
    return VirtualPathError::kNotNfc;
  }
  if (segment.front() == ' ' || segment.back() == ' ') {
    return VirtualPathError::kEdgeSpace;
  }
  if (segment.back() == '.') {
    return VirtualPathError::kTrailingDot;
  }
  if (IsDeviceName(segment)) {
    return VirtualPathError::kDeviceName;
  }
  if (segment.size() > kMaxVirtualPathSegmentBytes) {
    return VirtualPathError::kSegmentTooLong;
  }
  return std::nullopt;
}

auto ValidateMountName(const std::string_view mount)
  -> std::optional<VirtualPathError>
{
  const auto name = mount.starts_with('.') ? mount.substr(1) : mount;
  if (name.empty() || !IsIdentifierStart(name.front())
    || !std::ranges::all_of(name.substr(1), IsIdentifierChar)) {
    return VirtualPathError::kInvalidMountName;
  }
  if (mount.size() > kMaxVirtualPathSegmentBytes) {
    return VirtualPathError::kSegmentTooLong;
  }
  return std::nullopt;
}

auto ValidateVirtualPath(const std::string_view virtual_path,
  const VirtualPathRuleSet rules) -> std::optional<VirtualPathError>
{
  if (virtual_path.empty()) {
    return VirtualPathError::kEmpty;
  }
  if (virtual_path.front() != '/') {
    return VirtualPathError::kNotAbsolute;
  }
  if (virtual_path.size() > 1 && virtual_path.back() == '/') {
    return VirtualPathError::kTrailingSlash;
  }
  const auto segments = Split(virtual_path);
  if (std::ranges::any_of(segments, &std::string_view::empty)) {
    return VirtualPathError::kEmptySegment;
  }
  if (const auto error = ValidateMountName(segments.front());
    error.has_value()) {
    return error;
  }
  if (segments.size() == 1) {
    return VirtualPathError::kMountOnly;
  }
  for (auto index = std::size_t { 1 }; index < segments.size(); ++index) {
    const auto segment = segments[index];
    if (const auto error = ValidateVirtualPathSegment(segment);
      error.has_value()) {
      return error;
    }
    if (rules == VirtualPathRuleSet::kAuthored && IsDerivedContent(segment)) {
      return VirtualPathError::kDerivedContent;
    }
    if (index + 1 == segments.size()) {
      const auto dot = segment.rfind('.');
      if (dot == std::string_view::npos || dot == 0) {
        return VirtualPathError::kMissingExtension;
      }
    }
  }
  return std::nullopt;
}

auto IsCanonicalVirtualPath(
  const std::string_view virtual_path, const VirtualPathRuleSet rules) -> bool
{
  return !ValidateVirtualPath(virtual_path, rules).has_value();
}

auto ToNfc(const std::string_view text) -> std::optional<std::string>
{
  if (!Decode(text).has_value()) {
    return std::nullopt;
  }
  if (text.empty()) {
    return std::string {};
  }
  constexpr auto kOptions = static_cast<unsigned>(UTF8PROC_STABLE)
    | static_cast<unsigned>(UTF8PROC_COMPOSE);
  auto* raw = static_cast<utf8proc_uint8_t*>(nullptr);
  // utf8proc reads and writes UTF-8 as unsigned bytes.
  const auto length = utf8proc_map(
    reinterpret_cast<const utf8proc_uint8_t*>( // NOLINT(*-reinterpret-cast)
      text.data()),
    static_cast<utf8proc_ssize_t>(text.size()), &raw,
    static_cast<utf8proc_option_t>(kOptions));
  const auto owner = std::unique_ptr<utf8proc_uint8_t, decltype(&std::free)> {
    raw,
    &std::free,
  };
  if (length < 0 || !owner) {
    return std::nullopt;
  }
  return std::string {
    reinterpret_cast<const char*>(owner.get()), // NOLINT(*-reinterpret-cast)
    static_cast<std::size_t>(length),
  };
}

auto FoldCase(const std::string_view text) -> std::optional<std::string>
{
  auto codes = Decode(text);
  if (!codes.has_value()) {
    return std::nullopt;
  }
  std::ranges::transform(*codes, codes->begin(), Fold);
  return Encode(*codes);
}

} // namespace oxygen::content

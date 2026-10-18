//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Content/api_export.h>

//! Rules for virtual paths, path segments and mount names.
/*!
 The rules, their order and their refusal codes are stated in
 `Content/Docs/virtual_paths.md`. The managed implementation
 (`Oxygen.Managed.Core`) agrees with this one exactly: both test suites run
 `VirtualPathCases.json`, kept beside this header.
*/
namespace oxygen::content {

//! Why a virtual path, a segment or a mount name is refused.
enum class VirtualPathError : std::uint8_t {
  kEmpty,
  kNotAbsolute,
  kTrailingSlash,
  kEmptySegment,
  kDotSegment,
  kInvalidUtf8,
  kControlCharacter,
  kReservedCharacter,
  kUnassignedCodePoint,
  kNotNfc,
  kEdgeSpace,
  kTrailingDot,
  kDeviceName,
  kSegmentTooLong,
  kInvalidMountName,
  kMountOnly,
  kMissingExtension,
  kDerivedContent,
};

//! The stable refusal code, such as `vpath.segment.reserved_character`.
OXGN_CNTT_NDAPI auto to_string(VirtualPathError error) noexcept -> const char*;

//! Which rules a path is checked against.
enum class VirtualPathRuleSet : std::uint8_t {
  //! A path the engine writes or resolves.
  kCooked,
  //! A path being authored; adds the derived content rule.
  kAuthored,
};

//! The longest segment, in bytes of UTF-8: the portable file name bound.
inline constexpr std::size_t kMaxVirtualPathSegmentBytes = 255;

//! Checks one path segment against the segment rule.
OXGN_CNTT_NDAPI auto ValidateVirtualPathSegment(std::string_view segment)
  -> std::optional<VirtualPathError>;

//! Checks a mount name, without its leading `/`.
OXGN_CNTT_NDAPI auto ValidateMountName(std::string_view mount)
  -> std::optional<VirtualPathError>;

//! Checks a virtual path.
OXGN_CNTT_NDAPI auto ValidateVirtualPath(std::string_view virtual_path,
  VirtualPathRuleSet rules = VirtualPathRuleSet::kCooked)
  -> std::optional<VirtualPathError>;

//! True when `ValidateVirtualPath` accepts the path.
OXGN_CNTT_NDAPI auto IsCanonicalVirtualPath(std::string_view virtual_path,
  VirtualPathRuleSet rules = VirtualPathRuleSet::kCooked) -> bool;

//! The NFC form of UTF-8 text, or nothing when the text is not valid UTF-8.
OXGN_CNTT_NDAPI auto ToNfc(std::string_view text) -> std::optional<std::string>;

//! The Unicode 15.1 simple case folding of UTF-8 text, or nothing when the text
//! is not valid UTF-8.
OXGN_CNTT_NDAPI auto FoldCase(std::string_view text)
  -> std::optional<std::string>;

} // namespace oxygen::content

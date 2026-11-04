//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
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

// clang-format off
//! The stable refusal codes of the virtual path grammar.
namespace vpath_codes {
  inline constexpr std::string_view kEmpty = "vpath.empty";
  inline constexpr std::string_view kLeafMissingExtension = "vpath.leaf.missing_extension";
  inline constexpr std::string_view kMountInvalid = "vpath.mount.invalid";
  inline constexpr std::string_view kMountOnly = "vpath.mount_only";
  inline constexpr std::string_view kNotAbsolute = "vpath.not_absolute";
  inline constexpr std::string_view kReferenceOutsideMount = "vpath.reference_outside_mount";
  inline constexpr std::string_view kReferenceSelfAbsolute = "vpath.reference_self_absolute";
  inline constexpr std::string_view kSegmentControlCharacter = "vpath.segment.control_character";
  inline constexpr std::string_view kSegmentDerivedContent = "vpath.segment.derived_content";
  inline constexpr std::string_view kSegmentInvalidUtf8 = "vpath.segment.invalid_utf8";
  inline constexpr std::string_view kSegmentDeviceName = "vpath.segment.device_name";
  inline constexpr std::string_view kSegmentDot = "vpath.segment.dot";
  inline constexpr std::string_view kSegmentEdgeSpace = "vpath.segment.edge_space";
  inline constexpr std::string_view kSegmentEmpty = "vpath.segment.empty";
  inline constexpr std::string_view kSegmentNotNfc = "vpath.segment.not_nfc";
  inline constexpr std::string_view kSegmentReservedCharacter = "vpath.segment.reserved_character";
  inline constexpr std::string_view kSegmentTooLong = "vpath.segment.too_long";
  inline constexpr std::string_view kSegmentTrailingDot = "vpath.segment.trailing_dot";
  inline constexpr std::string_view kSegmentUnassignedCodePoint = "vpath.segment.unassigned_code_point";
  inline constexpr std::string_view kTrailingSlash = "vpath.trailing_slash";
  inline constexpr std::string_view kUnknown = "vpath.unknown";
} // namespace vpath_codes
// clang-format on

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
  kReferenceOutsideMount,
  kReferenceSelfAbsolute,
};

//! The stable refusal code, such as `vpath.segment.reserved_character`.
OXGN_CNTT_NDAPI auto to_string(VirtualPathError error) noexcept
  -> std::string_view;

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

//! The mount a virtual path lives in: its first segment, with the leading `/`.
/*!
 `/Content` for `/Content/Models/Ship.glb`. A loose cooked root holds one
 mount: the descriptor of `/Content/Models/Ship.glb` is `Models/Ship.glb` under
 it.

 @return The mount, or empty text for a path that does not start with `/`, or
   names a mount and nothing in it.
*/
OXGN_CNTT_NDAPI auto MountRootOf(std::string_view virtual_path) noexcept
  -> std::string_view;

//! The virtual path a reference names, read in the file whose identity is
//! `anchor`.
/*!
 Absolute text is a virtual path in another mount. Relative text is one `.` or
 zero or more `..` segments, then one or more canonical segments, and resolves
 against the folder that holds `anchor`: `../Textures/Brick.otex` in
 `/Content/Materials/Wall.omat` is `/Content/Textures/Brick.otex`. The result is
 checked against `rules`: asset references are authored paths; file references
 use the cooked rules, which accept derived content such as `SourceMedia`.

 @param anchor The identity of the referring file: a canonical virtual path.
 @param text The reference as written.
 @param rules The rules the resolved path is checked against.
 @return The resolved path; or the anchor's refusal, a refusal of the text's
   segments, `kReferenceOutsideMount` when the `..` segments climb above the
   anchor's mount, `kReferenceSelfAbsolute` for absolute text in the anchor's
   own mount, or the refusal of the resolved path.
*/
OXGN_CNTT_NDAPI auto ResolveReference(std::string_view anchor,
  std::string_view text,
  VirtualPathRuleSet rules = VirtualPathRuleSet::kAuthored)
  -> std::expected<std::string, VirtualPathError>;

//! The text that names `target` from the file whose identity is `anchor`.
/*!
 The shortest relative text when both are in one mount, such as `Brick.otex`
 or `../Textures/Brick.otex`, and the target's virtual path otherwise.
 `ResolveReference(anchor, RelativeReference(anchor, target))` is `target`.

 @param anchor The identity of the referring file: a canonical virtual path.
 @param target The identity the reference names: a canonical virtual path.
*/
OXGN_CNTT_NDAPI auto RelativeReference(
  std::string_view anchor, std::string_view target) -> std::string;

//! The NFC form of UTF-8 text, or nothing when the text is not valid UTF-8.
OXGN_CNTT_NDAPI auto ToNfc(std::string_view text) -> std::optional<std::string>;

//! The nearest name the segment rule accepts, for a name the engine assigns.
/*!
 Normalizes to NFC and replaces each refused character, invalid UTF-8 sequence
 and unassigned code point with `_`, as well as a leading `.`, a leading or
 trailing space and a trailing dot. A name whose part before its first dot is a
 device name gets `_` after that part (`NUL.v2` becomes `NUL_.v2`). Empty text
 stays empty, and the length is not bounded: the caller shortens the name to
 the leaf it builds. The cooker names imported model products with it.
*/
OXGN_CNTT_NDAPI auto ToSegmentName(std::string_view text) -> std::string;

//! The Unicode 15.1 simple case folding of UTF-8 text, or nothing when the text
//! is not valid UTF-8.
OXGN_CNTT_NDAPI auto FoldCase(std::string_view text)
  -> std::optional<std::string>;

} // namespace oxygen::content

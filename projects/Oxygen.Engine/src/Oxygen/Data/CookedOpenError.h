//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace oxygen::data {

//! Why a cooked container, a pak or a loose cooked index, cannot be opened:
//! what a caller can do about it.
enum class CookedOpenFailure : uint8_t {
  //! Not a file of that format: its magic differs, or it is too small to
  //! hold one.
  kWrongFormat = 0,
  //! A format version this build cannot read.
  kUnsupportedVersion,
  //! Of that format and version, but malformed.
  kCorrupt,
  //! Verification was asked for, but the container holds no integrity data
  //! to verify it against.
  kUnverifiable,
};

//! The rule a refused cooked artifact (a pak, its catalog and browse index, a
//! loose cooked index or a resource descriptor) breaks. Each defect belongs
//! to one `CookedOpenFailure`; see `FailureOf`.
enum class CookedDefect : uint8_t {
  //=== kWrongFormat ===----------------------------------------------------//

  //! No artifact of the format where one is expected, such as a folder
  //! without a loose cooked index.
  kAbsent,
  //! Too small to hold the header of its format.
  kTooSmall,
  //! A magic of another format.
  kWrongMagic,

  //=== kUnsupportedVersion ===---------------------------------------------//

  //! A format version this build does not read.
  kUnsupportedVersion,

  //=== kUnverifiable ===---------------------------------------------------//

  //! Verification was asked for, and there is no integrity data.
  kNoIntegrityData,

  //=== kCorrupt ===--------------------------------------------------------//

  //! A record or section ends before the bytes it needs.
  kTruncated,
  //! A size disagrees with the counts that declare it, or bytes trail it.
  kSizeMismatch,
  //! A section, payload, string or index lies outside what holds it.
  kOutOfBounds,
  //! A section or payload does not start on the boundary it requires.
  kMisaligned,
  //! Two sections share bytes, or sections that must be contiguous are not.
  kOverlap,
  //! A reserved byte is not zero.
  kReservedNotZero,
  //! A table records another row size, or does not hold whole rows.
  kWrongRowSize,
  //! A digest the format requires is all zero.
  kMissingDigest,
  //! A stored digest or CRC32 does not match the bytes it covers.
  kDigestMismatch,
  //! A source identity is not a UUIDv7.
  kInvalidIdentity,
  //! A path is not canonical.
  kInvalidPath,
  //! A string table is malformed: unterminated, not UTF-8, or an offset
  //! outside it.
  kInvalidString,
  //! A key, path or record appears twice where it must be unique.
  kDuplicate,
  //! A reference block is malformed, or binds a row outside its table.
  kInvalidReference,
  //! A value no format version defines: a kind, a flag, a type, a view.
  kInvalidValue,
  //! Records are not in the canonical order the format requires.
  kNotCanonical,
  //! Two parts of the artifact disagree, or it disagrees with what it
  //! describes.
  kInconsistent,
  //! A part the format requires is absent.
  kMissingPart,
};

//! The family of `defect`: what a caller can do about it.
[[nodiscard]] constexpr auto FailureOf(const CookedDefect defect) noexcept
  -> CookedOpenFailure
{
  switch (defect) {
  case CookedDefect::kAbsent:
  case CookedDefect::kTooSmall:
  case CookedDefect::kWrongMagic:
    return CookedOpenFailure::kWrongFormat;
  case CookedDefect::kUnsupportedVersion:
    return CookedOpenFailure::kUnsupportedVersion;
  case CookedDefect::kNoIntegrityData:
    return CookedOpenFailure::kUnverifiable;
  case CookedDefect::kTruncated:
  case CookedDefect::kSizeMismatch:
  case CookedDefect::kOutOfBounds:
  case CookedDefect::kMisaligned:
  case CookedDefect::kOverlap:
  case CookedDefect::kReservedNotZero:
  case CookedDefect::kWrongRowSize:
  case CookedDefect::kMissingDigest:
  case CookedDefect::kDigestMismatch:
  case CookedDefect::kInvalidIdentity:
  case CookedDefect::kInvalidPath:
  case CookedDefect::kInvalidString:
  case CookedDefect::kDuplicate:
  case CookedDefect::kInvalidReference:
  case CookedDefect::kInvalidValue:
  case CookedDefect::kNotCanonical:
  case CookedDefect::kInconsistent:
  case CookedDefect::kMissingPart:
    return CookedOpenFailure::kCorrupt;
  }
  return CookedOpenFailure::kCorrupt;
}

//! A cooked artifact that cannot be opened: the rule it breaks, and a
//! message for logs.
class CookedOpenError final : public std::runtime_error {
public:
  CookedOpenError(const CookedDefect defect, const std::string& message)
    : std::runtime_error(message)
    , defect_(defect)
  {
  }

  //! What a caller can do about the refusal.
  [[nodiscard]] auto Failure() const noexcept -> CookedOpenFailure
  {
    return FailureOf(defect_);
  }

  //! The rule the artifact breaks.
  [[nodiscard]] auto Defect() const noexcept -> CookedDefect { return defect_; }

private:
  CookedDefect defect_;
};

} // namespace oxygen::data

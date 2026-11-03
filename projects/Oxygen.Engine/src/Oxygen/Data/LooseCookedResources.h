//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/Region.h>

namespace oxygen::data::loose_cooked {

//! A resource's payload, as its resource table row describes it.
struct ResourcePayload final {
  //! The row of the table that describes the payload.
  uint32_t row = 0;
  //! Where the payload starts in its data file.
  uint64_t offset = 0;
  uint64_t size = 0;
  //! The SHA-256 its producer computed from the payload bytes.
  pak::core::ContentHashDigest sha256 {};
};

//! Whether `kind` is a resource data file, which every import appends to.
/*!
 A data file has no digest of its own in the index: each resource in it is
 verified against the SHA-256 its table row holds.
*/
OXGN_DATA_NDAPI auto IsResourceDataFile(FileKind kind) noexcept -> bool;

//! The data file kind whose payloads the rows of a `table` file describe;
//! `std::nullopt` when `table` is not a resource table kind.
OXGN_DATA_NDAPI auto DataFileKindOf(FileKind table) noexcept
  -> std::optional<FileKind>;

//! The size of one row of a `table` file.
/*!
 @throw std::invalid_argument when `table` is not a resource table kind.
*/
OXGN_DATA_NDAPI auto ResourceRowSize(FileKind table) -> size_t;

//! Whether `size` bytes can be the resource table `table`: whole rows, no
//! more of them than a resource index addresses.
/*!
 @throw std::invalid_argument when `table` is not a resource table kind.
*/
OXGN_DATA_NDAPI auto IsResourceTableSize(FileKind table, uint64_t size) -> bool;

//! The payloads that the rows of a `table` file describe, in row order.
/*!
 Rows of no payload (`size_bytes` 0), such as a table's reserved empty row,
 are left out: they hold no bytes to verify, and no digest.

 @throw std::invalid_argument when `table` is not a resource table kind.
 @throw CookedFormatError `kWrongRowSize` when `rows` ends in a partial row,
   `kMissingDigest` when a row of a payload has no digest, `kInvalidValue`
   when a texture row records a compression its format does not imply or an
   empty row records a digest.
*/
OXGN_DATA_NDAPI auto ReadResourcePayloads(FileKind table,
  std::span<const std::byte> rows) -> std::vector<ResourcePayload>;

//! Why a resource payload does not verify.
enum class ResourcePayloadFailure : uint8_t {
  //! The payload lies outside its data file.
  kOutOfBounds,
  //! The payload bytes do not hash to the row's SHA-256.
  kDigestMismatch,
};

//! A payload that does not verify.
struct ResourcePayloadIssue final {
  uint32_t row = 0;
  ResourcePayloadFailure reason = ResourcePayloadFailure::kOutOfBounds;
};

//! Verifies every payload against its row's SHA-256, reading each once.
/*!
 @param payloads The payloads of one resource table (`ReadResourcePayloads`).
 @param data The table's data file, as a device or a region.
 @return The payloads that do not verify, in row order.
 @throw std::runtime_error when `data` cannot be read.
*/
template <serio::ReadableDevice D>
[[nodiscard]] auto VerifyResourcePayloads(
  const std::span<const ResourcePayload> payloads, const D& data)
  -> std::vector<ResourcePayloadIssue>
{
  auto issues = std::vector<ResourcePayloadIssue> {};
  for (const auto& payload : payloads) {
    const auto region = serio::Region<D>::Make(data,
      serio::ByteRange {
        .offset = serio::ByteOffset { payload.offset },
        .count = serio::ByteCount { payload.size },
      });
    if (!region) {
      if (region.error()
        != serio::make_error_code(serio::Error::kOutOfBounds)) {
        throw std::runtime_error(
          "cannot read a resource data file: " + region.error().message());
      }
      issues.push_back({
        .row = payload.row,
        .reason = ResourcePayloadFailure::kOutOfBounds,
      });
      continue;
    }
    const auto digest = serio::ComputeSha256(*region);
    if (!digest) {
      throw std::runtime_error(
        "cannot hash a resource payload: " + digest.error().message());
    }
    if (*digest != payload.sha256) {
      issues.push_back({
        .row = payload.row,
        .reason = ResourcePayloadFailure::kDigestMismatch,
      });
    }
  }
  return issues;
}

} // namespace oxygen::data::loose_cooked

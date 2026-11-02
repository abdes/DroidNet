//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>

#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! The content hash of an asset descriptor: the SHA-256 of all its bytes,
//! taken with `pak::core::AssetHeader::content_hash` zeroed.
/*!
 @throw CookedOpenError `kTooSmall` when `descriptor` is smaller than its
   asset header.
*/
OXGN_DATA_NDAPI auto ComputeDescriptorHash(
  std::span<const std::byte> descriptor) -> pak::core::ContentHashDigest;

//! Records in the asset header of `descriptor` its content hash.
/*!
 @throw CookedOpenError `kTooSmall` when `descriptor` is smaller than its
   asset header.
*/
OXGN_DATA_API auto StampDescriptorHash(std::span<std::byte> descriptor) -> void;

//! Checks that the asset header of `descriptor` records its content hash.
/*!
 @throw CookedOpenError `kTooSmall` when `descriptor` is smaller than its
   asset header, `kMissingDigest` when the header records no hash, and
   `kDigestMismatch` when it records another one.
*/
OXGN_DATA_API auto CheckDescriptorHash(std::span<const std::byte> descriptor)
  -> void;

} // namespace oxygen::data

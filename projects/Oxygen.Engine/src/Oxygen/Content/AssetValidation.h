//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>

#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content {

//! Validate a current descriptor and its complete reference inventory using
//! the runtime typed decoders. Does not read or load dependency payloads.
//! Throws std::runtime_error for malformed or inconsistent data.
OXGN_CNTT_API auto ValidateAssetDescriptor(data::AssetType type,
  const data::AssetKey& key, std::span<const std::byte> bytes,
  const data::AssetReferences& references) -> void;

} // namespace oxygen::content

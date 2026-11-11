//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::pakdump {

//! Decodes the descriptor of `entry` as the runtime does: the runtime's
//! validation checks it, then `load`, the runtime loader of its type, reads
//! it. PakDump prints what the runtime reads; it walks no layout of its own.
/*!
 @throw data::CookedFormatError when the runtime refuses the descriptor.
*/
template <typename Load>
[[nodiscard]] auto DecodeAsset(const PakFile& pak,
  const data::pak::core::AssetDirectoryEntry& entry, Load&& load)
{
  const auto bytes = pak.DescriptorBytes(entry);
  const auto references = pak.ReadAssetReferences(entry.asset_key);
  ValidateAssetDescriptor(entry.asset_type, entry.asset_key, bytes, references);
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  const auto context = LoaderContext {
    .current_asset_key = entry.asset_key,
    .desc_reader = &reader,
    .asset_references = observer_ptr(&references),
    .work_offline = true,
    .parse_only = true,
  };
  return std::forward<Load>(load)(context);
}

} // namespace oxygen::content::pakdump

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>
#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>

namespace oxygen::content::loaders {

//! Loader function for physics scene sidecar assets.
/*!
  Reads a `PhysicsSceneAssetDesc` from the asset descriptor stream and
  constructs a `data::PhysicsSceneAsset` (zero-copy, owned data blob).

  ### Constraints
  - The calling loader context must have `desc_reader` positioned at the start
    of the asset's descriptor block.
  - Parse-only mode is respected: in that mode, validation still runs but no
    dependency edges are registered.

  ### Error Contract
  Throws `std::runtime_error` on any structural violation. Callers (AssetLoader)
  catch these and report a null asset to subscribers.

  @see LoadSceneAsset, data::PhysicsSceneAsset
*/
inline auto LoadPhysicsSceneAsset(const LoaderContext& context)
  -> std::unique_ptr<data::PhysicsSceneAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto packed = reader.ScopedAlignment(1);

  const auto base_pos_res = reader.Position();
  CheckLoaderResult(base_pos_res, "physics scene asset", "Position(base)");
  const size_t base_pos = *base_pos_res;

  // Read the fixed descriptor to compute the total payload size.
  data::pak::physics::PhysicsSceneAssetDesc desc {};
  {
    auto blob_res = reader.ReadBlob(sizeof(desc));
    CheckLoaderResult(
      blob_res, "physics scene asset", "ReadBlob(PhysicsSceneAssetDesc)");
    std::memcpy(&desc, (*blob_res).data(), sizeof(desc));
  }

  // Validate asset type.
  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kPhysicsScene) {
    throw std::runtime_error(
      "invalid asset_type in PhysicsSceneAssetDesc header");
  }

  // Validate descriptor version.
  if (desc.header.version != data::pak::physics::kPhysicsSceneAssetVersion) {
    throw std::runtime_error(
      "unsupported PhysicsSceneAssetDesc descriptor version");
  }

  // Compute the full payload extent from the component table directory.
  size_t payload_end = sizeof(data::pak::physics::PhysicsSceneAssetDesc);

  if (desc.component_table_count > 0) {
    const size_t dir_bytes = static_cast<size_t>(desc.component_table_count)
      * sizeof(data::pak::physics::PhysicsComponentTableDesc);

    AddRangeEnd(payload_end,
      static_cast<size_t>(desc.component_table_directory_offset), dir_bytes);

    // Seek and scan the table directory to account for each binding table.
    auto seek_res = reader.Seek(
      base_pos + static_cast<size_t>(desc.component_table_directory_offset));
    CheckLoaderResult(
      seek_res, "physics scene asset", "Seek(component_table_directory)");

    for (uint32_t i = 0; i < desc.component_table_count; ++i) {
      data::pak::physics::PhysicsComponentTableDesc entry {};
      auto entry_blob = reader.ReadBlob(sizeof(entry));
      CheckLoaderResult(entry_blob, "physics scene asset",
        "ReadBlob(PhysicsComponentTableDesc)");
      std::memcpy(&entry, (*entry_blob).data(), sizeof(entry));

      if (entry.table.count > 0) {
        const size_t table_bytes
          = static_cast<size_t>(entry.table.count) * entry.table.entry_size;
        AddRangeEnd(
          payload_end, static_cast<size_t>(entry.table.offset), table_bytes);
      }
    }
  }

  // Rewind and read the full payload blob.
  {
    auto seek_res = reader.Seek(base_pos);
    CheckLoaderResult(seek_res, "physics scene asset", "Seek(base)");
  }

  auto blob_res = reader.ReadBlob(payload_end);
  CheckLoaderResult(
    blob_res, "physics scene asset", "ReadBlob(physics_scene_payload)");
  std::vector<std::byte> bytes = std::move(*blob_res);

  LOG_F(1, "physics scene asset payload: {} bytes", bytes.size());

  // Construct the in-memory asset (validates all ranges internally).
  return std::make_unique<data::PhysicsSceneAsset>(
    context.current_asset_key, std::move(bytes));
}

} // namespace oxygen::content::loaders

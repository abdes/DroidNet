//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/api_export.h>
#include <memory>

namespace oxygen::content::loaders {

//! Placeholder mesh asset type for loader demonstration.
class PlaceHolderMeshAsset : public oxygen::Object {
  OXYGEN_TYPED(PlaceHolderMeshAsset)
public:
  PlaceHolderMeshAsset() = default;
  ~PlaceHolderMeshAsset() override = default;
};

//! Loader for mesh assets.
OXGN_CNTT_API std::unique_ptr<PlaceHolderMeshAsset> LoadMesh(
  const PakFile& pak, const AssetDirectoryEntry& entry);

} // namespace oxygen::content::loaders

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

//! Placeholder geometry asset type for loader demonstration.
class PlaceHolderGeometryAsset : public oxygen::Object {
  OXYGEN_TYPED(PlaceHolderGeometryAsset)
public:
  PlaceHolderGeometryAsset() = default;
  ~PlaceHolderGeometryAsset() override = default;
};

//! Loader for geometry assets.
OXGN_CNTT_API std::unique_ptr<PlaceHolderGeometryAsset> LoadGeometry(
  const PakFile& pak, const AssetDirectoryEntry& entry);

} // namespace oxygen::content::loaders

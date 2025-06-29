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

//! Placeholder shader asset type for loader demonstration.
class PlaceHolderShaderAsset : public oxygen::Object {
  OXYGEN_TYPED(PlaceHolderShaderAsset)
public:
  PlaceHolderShaderAsset() = default;
  ~PlaceHolderShaderAsset() override = default;
};

//! Loader for shader assets.
OXGN_CNTT_API std::unique_ptr<PlaceHolderShaderAsset> LoadShader(
  const PakFile& pak, const AssetDirectoryEntry& entry);

} // namespace oxygen::content::loaders

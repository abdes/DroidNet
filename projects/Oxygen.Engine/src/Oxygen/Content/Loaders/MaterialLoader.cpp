//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Loaders/MaterialLoader.h>

namespace oxygen::content::loaders {

std::unique_ptr<PlaceHolderMaterialAsset> LoadMaterial(
  const PakFile& pak, const AssetDirectoryEntry& entry)
{
  // TODO: Implement material asset loading
  return std::make_unique<PlaceHolderMaterialAsset>();
}

} // namespace oxygen::content::loaders

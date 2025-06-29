//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Loaders/GeometryLoader.h>

namespace oxygen::content::loaders {

std::unique_ptr<PlaceHolderGeometryAsset> LoadGeometry(
  const PakFile& pak, const AssetDirectoryEntry& entry)
{
  // TODO: Implement geometry asset loading
  return std::make_unique<PlaceHolderGeometryAsset>();
}

} // namespace oxygen::content::loaders

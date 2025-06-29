//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Loaders/ShaderLoader.h>

namespace oxygen::content::loaders {

std::unique_ptr<PlaceHolderShaderAsset> LoadShader(
  const PakFile& pak, const AssetDirectoryEntry& entry)
{
  // TODO: Implement shader asset loading
  return std::make_unique<PlaceHolderShaderAsset>();
}

} // namespace oxygen::content::loaders

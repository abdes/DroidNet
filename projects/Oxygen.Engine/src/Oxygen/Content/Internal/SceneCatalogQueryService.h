//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Content/IAssetLoader.h>

namespace oxygen::content::internal {

class ContentSourceRegistry;

class SceneCatalogQueryService final {
public:
  [[nodiscard]] auto EnumerateMountedScenes(
    const ContentSourceRegistry& source_registry) const
    -> std::vector<IAssetLoader::MountedSceneEntry>;
};

} // namespace oxygen::content::internal

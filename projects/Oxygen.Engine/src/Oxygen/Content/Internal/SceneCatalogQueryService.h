//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Content/IAssetLoader.h>

namespace oxygen::content::internal {

class ContentSourceView;

//! Scene queries over one captured view of the mounts.
/*!
 The catalog reads the mounted indexes only. The hierarchy and node reads parse
 scene descriptors; they read only through the view's sources, so they can run
 on any thread while the view is held.
*/
class SceneCatalogQueryService final {
public:
  [[nodiscard]] static auto EnumerateMountedScenes(
    const ContentSourceView& view)
    -> std::vector<IAssetLoader::MountedSceneEntry>;

  //! The hierarchy of the mounted scene `key`, read from scene descriptors.
  [[nodiscard]] static auto ReadSceneHierarchy(const ContentSourceView& view,
    const data::AssetKey& key) -> std::optional<IAssetLoader::SceneHierarchy>;

  //! The effective values of the node `path` names in the composition of the
  //! mounted scene `key`, read from scene descriptors.
  [[nodiscard]] static auto ReadSceneNode(const ContentSourceView& view,
    const data::AssetKey& key, std::span<const data::NodeId> path)
    -> std::optional<IAssetLoader::SceneNodeValues>;
};

} // namespace oxygen::content::internal

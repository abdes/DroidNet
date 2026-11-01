//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "DemoShell/UI/ContentTextMatch.h"

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::examples::ui {

//! A scene the mounted sources hold, from the top source that holds it.
struct SceneEntry final {
  //! The scene's file name without its extension, such as `BottleOnBox`.
  std::string name;
  std::string virtual_path;
  //! The band it is listed under: the first folder below the mount, such as
  //! `Scenes`; empty for a scene directly in its mount.
  std::string folder;
  //! Its row: the folders between the band and the scene, which name the
  //! scene or the model it was imported from, such as `BottleOnBox` or
  //! `KenneyAnimatedCharacters / idle.fbx`; the name when there are none.
  std::string label;
  data::AssetKey key;
  data::CookedSourceKind source_kind { data::CookedSourceKind::kLooseCooked };
  //! The pak file or the loose cooked root folder.
  std::filesystem::path source_path;
};

//! The scenes of one mount, such as `/Showcase`.
struct SceneGroup final {
  std::string mount;
  //! Its scenes, by virtual path.
  std::vector<SceneEntry> scenes;
  bool from_pak { false };
  bool from_loose_root { false };
};

//! The assets a load of the scene `key` reads: the scene and every asset it
//! references, transitively, each from the first of `sources` (pak files and
//! loose roots, top first) that holds it. Sources that cannot be read are
//! skipped.
[[nodiscard]] auto CountSceneAssets(const data::AssetKey& key,
  const std::vector<std::filesystem::path>& sources) -> std::size_t;

//! The scene list of the Scenes tab: `IAssetLoader::EnumerateMountedScenes`,
//! grouped by mount.
class ContentSceneCatalog final {
public:
  //! The row of the mount list a source path is, lower wins; none for a
  //! source outside the list.
  using Precedence
    = std::function<std::optional<std::size_t>(const std::filesystem::path&)>;

  //! Keeps, per scene key, the scene of the top mounted source.
  auto Rebuild(
    const std::vector<content::IAssetLoader::MountedSceneEntry>& scenes,
    const Precedence& precedence) -> void;

  [[nodiscard]] auto Groups() const noexcept -> const std::vector<SceneGroup>&
  {
    return groups_;
  }

  [[nodiscard]] auto Count() const -> std::size_t;

  [[nodiscard]] auto Find(const data::AssetKey& key) const -> const SceneEntry*;

  //! True when the name or the virtual path matches `filter`.
  [[nodiscard]] static auto Matches(
    const SceneEntry& scene, const TextFilter& filter) -> bool;

private:
  std::vector<SceneGroup> groups_;
};

} // namespace oxygen::examples::ui

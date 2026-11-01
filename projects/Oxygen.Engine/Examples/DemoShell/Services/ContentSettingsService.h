//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::examples {

//! Every import setting the content panel edits: what an import writes and
//! the importer that runs it.
struct ImportProfile final {
  content::import::ImportOptions options;
  content::import::ImportOptions::TextureTuning tuning;
  content::import::AsyncImportService::Config importer;
};

//! A pipeline of the importer, its settings and command name, its label and
//! its configuration member.
struct ImporterPipeline final {
  std::string_view name;
  std::string_view label;
  content::import::ImportPipelineConcurrency
    content::import::ImportConcurrency::* member;
};

inline constexpr auto kImporterPipelines = std::array {
  ImporterPipeline {
    .name = "texture",
    .label = "Texture",
    .member = &content::import::ImportConcurrency::texture,
  },
  ImporterPipeline {
    .name = "buffer",
    .label = "Buffer",
    .member = &content::import::ImportConcurrency::buffer,
  },
  ImporterPipeline {
    .name = "material",
    .label = "Material",
    .member = &content::import::ImportConcurrency::material,
  },
  ImporterPipeline {
    .name = "mesh",
    .label = "Mesh",
    .member = &content::import::ImportConcurrency::mesh_build,
  },
  ImporterPipeline {
    .name = "geometry",
    .label = "Geometry",
    .member = &content::import::ImportConcurrency::geometry,
  },
  ImporterPipeline {
    .name = "scene",
    .label = "Scene",
    .member = &content::import::ImportConcurrency::scene,
  },
};

//! The profile of a fresh install: the import defaults, a texture tuning for
//! interactive imports and the panel's importer configuration.
[[nodiscard]] auto DefaultImportProfile() -> ImportProfile;

//! A cooked source the developer added to the mount list.
struct ContentSourceSetting final {
  data::CookedSourceKind kind { data::CookedSourceKind::kLooseCooked };
  //! The pak file or the loose cooked root folder.
  std::filesystem::path path;

  auto operator==(const ContentSourceSetting&) const -> bool = default;
};

//! A loaded scene, as the panel finds it again at the next launch.
struct ContentSceneSetting final {
  std::string scene_key;
  std::string virtual_path;
  //! The cooked source it was loaded from: a pak file, or a loose source by
  //! its root folder.
  ContentSourceSetting source;

  auto operator==(const ContentSceneSetting&) const -> bool = default;
};

//! A recently loaded scene.
struct RecentSceneSetting final {
  std::string scene_key;
  std::string virtual_path;

  auto operator==(const RecentSceneSetting&) const -> bool = default;
};

//! The Import tab's model list: its source mount, filter and formats.
struct ContentImportTabSettings final {
  std::string source_mount;
  std::string filter;
  bool fbx { true };
  bool glb { true };
  bool gltf { true };
  //! Load the imported scene when an import finishes.
  bool load_scene_after_import { true };

  auto operator==(const ContentImportTabSettings&) const -> bool = default;
};

//! Persists the content panel's state in the demo's settings.
/*!
 Keys: `content.mounts.sources` (the mount list, top row first),
 `content.mounts.import_records`, `content.scene.*` (the loaded scene),
 `content.scene.recent`, `content.import.*`, `content.importer.*` and
 `content.import_tab.*`. Values out of range read as their defaults.
*/
class ContentSettingsService {
public:
  //! The scenes `GetRecentScenes` keeps.
  static constexpr std::size_t kRecentSceneCount = 3;

  ContentSettingsService() = default;
  virtual ~ContentSettingsService() = default;

  OXYGEN_MAKE_NON_COPYABLE(ContentSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(ContentSettingsService)

  //! The mount list, in precedence order: the first source wins.
  [[nodiscard]] virtual auto GetMountSources() const
    -> std::vector<ContentSourceSetting>;
  virtual auto SetMountSources(const std::vector<ContentSourceSetting>& sources)
    -> void;

  //! The model import records whose selected generation is mounted.
  [[nodiscard]] virtual auto GetMountedImportRecords() const
    -> std::vector<std::filesystem::path>;
  virtual auto SetMountedImportRecords(
    const std::vector<std::filesystem::path>& records) -> void;

  [[nodiscard]] virtual auto GetLoadedScene() const
    -> std::optional<ContentSceneSetting>;
  virtual auto SetLoadedScene(const std::optional<ContentSceneSetting>& scene)
    -> void;

  //! The last scenes loaded, newest first.
  [[nodiscard]] virtual auto GetRecentScenes() const
    -> std::vector<RecentSceneSetting>;
  virtual auto SetRecentScenes(const std::vector<RecentSceneSetting>& scenes)
    -> void;

  [[nodiscard]] virtual auto GetImportProfile() const -> ImportProfile;
  virtual auto SetImportProfile(const ImportProfile& profile) -> void;

  [[nodiscard]] virtual auto GetImportTab() const -> ContentImportTabSettings;
  virtual auto SetImportTab(const ContentImportTabSettings& settings) -> void;

  //! Returns the current settings epoch for cache invalidation.
  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t;

protected:
  auto BumpEpoch() noexcept -> void;

private:
  mutable std::atomic<std::uint64_t> epoch_ { 0 };
};

} // namespace oxygen::examples

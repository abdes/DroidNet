//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DemoShell/Runtime/ContentMountApplier.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include "DemoShell/UI/ContentLog.h"
#include "DemoShell/UI/ContentModelCatalog.h"
#include "DemoShell/UI/ContentMountList.h"
#include "DemoShell/UI/ContentSceneCatalog.h"
#include "DemoShell/UI/ContentSourceFolders.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Core/Types/AssetKey.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::examples::ui {

//! The scene on screen, as the Scenes tab and the status strip show it.
struct LoadedScene final {
  SceneEntry scene;
  //! The scene and the assets it references, transitively.
  std::size_t asset_count { 0 };
  std::chrono::milliseconds load_time { 0 };
};

//! The running import, as the Import tab and the status strip show it.
struct ImportProgressView final {
  std::filesystem::path model;
  std::string phase;
  //! The item being worked on, such as `Texture Rust.png`; empty between
  //! items.
  std::string item;
  float progress { 0.0F };
};

//! The content panel's view model.
/*!
 It orchestrates the panel's models (the mount list, the scene and model
 catalogs, the log) and the engine: the host applies the mount list
 (`SetOnMountsRequested`, answered by `OnMountsApplied`), mounts published
 generations and loads scenes. Every method runs on the UI thread; import
 progress and completion arrive from the importer and are taken in `Update`.
*/
class ContentVm {
public:
  //! What a file dialog opened by the panel picks.
  enum class BrowseTarget : uint8_t {
    kPakFile,
    kCookedRoot,
    kSourceFolder,
    kModelFile,
  };

  //! A file or folder picked in a dialog the panel opened.
  struct BrowseSelection final {
    BrowseTarget target { BrowseTarget::kPakFile };
    std::filesystem::path path;
  };

  //! Which settings an import of a model uses.
  enum class ImportSettingsSource : uint8_t {
    //! The panel's, saved to the model's record first.
    kPanel,
    //! The record's own.
    kRecord,
  };

  ContentVm(observer_ptr<ContentSettingsService> settings,
    observer_ptr<FileBrowserService> file_browser,
    observer_ptr<content::IAssetLoader> asset_loader);
  ~ContentVm();

  OXYGEN_MAKE_NON_COPYABLE(ContentVm)
  OXYGEN_MAKE_NON_MOVABLE(ContentVm)

  // --- Host ---

  //! The host replaces the loader's mounts with the list, lowest precedence
  //! first, through `runtime::ApplyContentMounts`, and answers with
  //! `OnMountsApplied`.
  auto SetOnMountsRequested(
    std::function<void(std::vector<content::ContentMount>)> callback) -> void;
  //! The host mounts a model import's new generation and answers with
  //! `OnGenerationMounted`. Without it, the VM mounts it on its loader.
  auto SetOnGenerationPublished(
    std::function<void(const runtime::GenerationPublication&)> callback)
    -> void;
  auto SetOnSceneLoadRequested(std::function<void(const SceneEntry&)> callback)
    -> void;
  auto SetOnSceneLoadCancelRequested(std::function<void()> callback) -> void;
  auto SetOnSceneUnloadRequested(std::function<void()> callback) -> void;

  auto OnMountsApplied(const runtime::MountOutcome& outcome) -> void;
  auto OnGenerationMounted(const runtime::GenerationPublication& publication,
    const std::optional<std::string>& error) -> void;
  //! The host's scene load finished; `error` says why a failed one failed.
  auto NotifySceneLoadCompleted(const data::AssetKey& key, bool success,
    std::string_view error = {}) -> void;

  //! Mounts the saved list and records and loads the saved scene once its
  //! source is mounted. Runs once.
  auto RestorePersistedState() -> void;

  //! Takes import progress and completion, and applies a deferred remount.
  auto Update() -> void;

  // --- Mounts ---

  [[nodiscard]] auto Mounts() const noexcept -> const ContentMountList&
  {
    return mounts_;
  }
  //! Adds a pak or a loose cooked root on top of the added sources.
  auto AddSource(const ContentSourceSetting& source) -> void;
  auto RemoveSource(std::size_t row) -> void;
  auto MoveSource(ContentMountList::RowMove move) -> void;
  //! Applies the list again; refused, missing and unreadable sources are
  //! retried.
  auto Remount() -> void;
  [[nodiscard]] auto IsMounting() const noexcept -> bool { return mounting_; }
  //! The assets and content version of row `row`, read once; null when the
  //! source cannot be read, with `error` saying why.
  [[nodiscard]] auto SourceDetailsOf(std::size_t row, std::string& error)
    -> const SourceDetails*;

  //! The folders models are imported from, by name.
  [[nodiscard]] auto SourceFolders() const -> std::vector<SourceFolder>;
  //! Adds `folder` as a source folder of this machine, in the scratch
  //! project's `.mounts.json`; returns its mount name, or nothing when it
  //! could not be added, which the log says why.
  auto AddSourceFolder(const std::filesystem::path& folder)
    -> std::optional<std::string>;
  //! Removes a source folder the panel added.
  auto RemoveSourceFolder(std::string_view name) -> void;

  auto BrowseForPak() -> void;
  auto BrowseForCookedRoot() -> void;
  auto BrowseForSourceFolder() -> void;
  auto BrowseForModelFile() -> void;
  //! The file picked in the open dialog, once; the panel runs the matching
  //! command with it.
  [[nodiscard]] auto ConsumeBrowseSelection() -> std::optional<BrowseSelection>;

  // --- Scenes ---

  [[nodiscard]] auto Scenes() const noexcept -> const ContentSceneCatalog&
  {
    return scenes_;
  }
  [[nodiscard]] auto RecentScenes() const -> std::vector<RecentSceneSetting>;
  [[nodiscard]] auto GetLoadedScene() const noexcept
    -> const std::optional<LoadedScene>&
  {
    return loaded_scene_;
  }
  auto RequestSceneLoad(const SceneEntry& scene) -> void;
  auto ReloadScene() -> void;
  auto UnloadScene() -> void;
  auto CancelSceneLoad() -> void;
  [[nodiscard]] auto IsSceneLoading() const noexcept -> bool
  {
    return scene_load_.has_value();
  }
  //! The scene being loaded; null when none.
  [[nodiscard]] auto LoadingScene() const noexcept -> const SceneEntry*;

  // --- Import ---

  [[nodiscard]] auto Models() const noexcept -> const ContentModelCatalog&
  {
    return models_;
  }
  [[nodiscard]] auto GetImportTab() const noexcept
    -> const ContentImportTabSettings&
  {
    return import_tab_;
  }
  //! Saves the tab's settings and lists the models they select.
  auto SetImportTab(const ContentImportTabSettings& tab) -> void;
  auto RefreshModels() -> void;
  //! The record of the model `file`, with its difference from the panel's
  //! settings.
  [[nodiscard]] auto ModelRecordOf(const std::filesystem::path& file)
    -> const ModelRecordView&;

  [[nodiscard]] auto GetImportProfile() const noexcept -> const ImportProfile&
  {
    return profile_;
  }
  auto SetImportProfile(const ImportProfile& profile) -> void;
  [[nodiscard]] auto ChangedImportGroups() const
    -> std::vector<ImportSettingsGroup>;
  auto ResetImportGroup(ImportSettingsGroup group) -> void;
  //! True when the importer settings differ from the running importer's.
  [[nodiscard]] auto IsImporterRestartPending() const -> bool;
  auto RestartImportService() -> void;

  auto StartImport(const std::filesystem::path& model,
    ImportSettingsSource settings = ImportSettingsSource::kPanel) -> void;
  //! Imports the model `file` with the panel's settings, from anywhere: a
  //! file in no source folder has its folder added as one first, since its
  //! record names it through a source mount. Lists that folder's models.
  auto ImportFile(const std::filesystem::path& file) -> void;
  auto CancelActiveImport() -> void;
  [[nodiscard]] auto IsImportInProgress() const -> bool;
  [[nodiscard]] auto GetImportProgress() const -> ImportProgressView;

  //! The scratch project's model generations folder; empty without one.
  [[nodiscard]] auto GetGeneratedStorageRoot() const -> std::filesystem::path;

  // --- Log ---

  [[nodiscard]] auto Log() const noexcept -> const ContentLog& { return log_; }
  auto ClearLog() -> void;
  auto MarkLogSeen() -> void;

  [[nodiscard]] auto GetFileBrowser() const -> observer_ptr<FileBrowserService>;

private:
  struct ImportJob final {
    std::atomic_bool running { false };
    std::filesystem::path model;
    content::import::ImportJobId job_id { content::import::kInvalidJobId };
    mutable std::mutex mutex;
    content::import::ProgressEvent progress {};
    std::optional<content::import::ImportReport> report;
  };

  struct SceneLoad final {
    SceneEntry scene;
    std::chrono::steady_clock::time_point started;
  };

  auto RequestMounts() -> void;
  //! Reads the scratch project's mounts again, with its `.mounts.json`.
  auto LoadScratchMounts() -> void;
  [[nodiscard]] auto LocalMounts() const -> std::filesystem::path;
  auto ResolveRecords(const std::vector<std::filesystem::path>& records)
    -> void;
  auto PersistMounts() const -> void;
  auto RefreshScenes() -> void;
  auto TryRestoreScene() -> void;
  auto RememberLoadedScene(const LoadedScene& loaded) -> void;
  auto RefreshModelStates() -> void;
  [[nodiscard]] auto PanelSettingsJson() const -> std::string;
  [[nodiscard]] auto Generations() const
    -> content::import::ModelImportGenerations;
  auto FinishImport(const content::import::ImportReport& report) -> void;
  [[nodiscard]] auto PublishGeneration(
    const content::import::ImportReport& report) -> bool;
  auto LoadImportedScene() -> void;
  auto OpenBrowser(const FileBrowserConfig& config, BrowseTarget target)
    -> void;

  observer_ptr<ContentSettingsService> settings_;
  observer_ptr<FileBrowserService> file_browser_;
  observer_ptr<content::IAssetLoader> asset_loader_;

  ContentMountList mounts_;
  ContentSceneCatalog scenes_;
  ContentModelCatalog models_;
  ContentLog log_;

  bool restored_ { false };
  bool mounting_ { false };
  bool remount_pending_ { false };
  //! The virtual paths of the last import's products, kept until its
  //! generation is mounted when its scene loads after the import.
  std::vector<std::string> imported_products_;
  std::map<std::filesystem::path, SourceDetails> details_;
  std::map<std::filesystem::path, std::string> details_errors_;

  std::optional<SceneLoad> scene_load_;
  std::optional<LoadedScene> loaded_scene_;
  std::optional<ContentSceneSetting> scene_to_restore_;

  std::unique_ptr<content::import::AsyncImportService> import_service_;
  content::import::AsyncImportService::Config running_importer_ {};
  ImportJob import_;

  ImportProfile profile_;
  ContentImportTabSettings import_tab_;
  bool model_states_dirty_ { false };
  bool import_cancel_requested_ { false };
  std::optional<ModelRecordView> record_view_;
  std::filesystem::path record_view_model_;

  std::optional<BrowseTarget> browse_target_;
  FileBrowserService::RequestId browse_request_id_ { 0 };

  std::function<void(std::vector<content::ContentMount>)> on_mounts_requested_;
  std::function<void(const runtime::GenerationPublication&)>
    on_generation_published_;
  std::function<void(const SceneEntry&)> on_scene_load_requested_;
  std::function<void()> on_scene_load_cancel_requested_;
  std::function<void()> on_scene_unload_requested_;
};

} // namespace oxygen::examples::ui

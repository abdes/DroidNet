//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Data/AssetKey.h>

#include "DemoShell/Services/ContentSettingsService.h"

namespace oxygen::examples {
class FileBrowserService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! Represents a scene that can be loaded.
struct SceneEntry {
  std::string name;
  data::AssetKey key;
};

//! Represents a source file (FBX, GLB, etc.) that can be imported.
struct ContentSource {
  std::filesystem::path path;
  content::import::ImportFormat format;
};

//! View Model orchestrating all content-related operations.
class ContentVm {
public:
  explicit ContentVm(observer_ptr<ContentSettingsService> settings_service,
    observer_ptr<FileBrowserService> file_browser_service);
  ~ContentVm();

  // --- Discovery (Sources & Mounts) ---

  //! Scans for importable source files based on explorer settings.
  auto RefreshSources() -> void;
  [[nodiscard]] auto GetSources() const -> const std::vector<ContentSource>&;

  //! Unified Refresh for all mountable content (PAKs, Loose Cooked).
  auto RefreshLibrary() -> void;

  // --- Task Management (Importing) ---

  auto StartImport(const std::filesystem::path& source_path) -> void;
  auto CancelActiveImport() -> void;

  [[nodiscard]] auto IsImportInProgress() const -> bool;
  [[nodiscard]] auto GetActiveImportPath() const -> std::string;
  [[nodiscard]] auto GetActiveImportProgress() const -> float;
  [[nodiscard]] auto GetActiveImportMessage() const -> std::string;

  // --- Browsing ---

  auto BrowseForModelRoot() -> void;
  auto BrowseForSourceFile() -> void;

  // --- Registry & Ready-to-Load ---

  [[nodiscard]] auto GetAvailableScenes() const
    -> const std::vector<SceneEntry>&;
  auto RequestSceneLoad(const data::AssetKey& key) -> void;

  [[nodiscard]] auto IsSceneLoading() const -> bool;
  [[nodiscard]] auto GetSceneLoadProgress() const -> float;
  [[nodiscard]] auto GetSceneLoadMessage() const -> std::string;
  [[nodiscard]] auto ShouldShowSceneLoadProgress() const -> bool;
  auto CancelSceneLoad() -> void;
  auto NotifySceneLoadCompleted(const data::AssetKey& key, bool success)
    -> void;

  // --- Mounting & Library ---

  auto SetOnPakMounted(
    std::function<void(const std::filesystem::path&)> callback) -> void;
  auto SetOnIndexLoaded(
    std::function<void(const std::filesystem::path&)> callback) -> void;

  auto MountPak(const std::filesystem::path& path) -> void;
  auto LoadIndex(const std::filesystem::path& path) -> void;
  auto UnloadAllLibrary() -> void;

  [[nodiscard]] auto GetDiscoveredPaks() const
    -> const std::vector<std::filesystem::path>&;
  [[nodiscard]] auto GetLoadedPaks() const
    -> const std::vector<std::filesystem::path>&;
  [[nodiscard]] auto GetLoadedIndices() const
    -> const std::vector<std::filesystem::path>&;

  auto BrowseForPak() -> void;
  auto BrowseForIndex() -> void;

  //! Register callback for scene load requests.
  auto SetOnSceneLoadRequested(
    std::function<void(const data::AssetKey&)> callback) -> void;
  //! Register callback for scene load cancellation requests.
  auto SetOnSceneLoadCancelRequested(std::function<void()> callback) -> void;

  // --- Settings & Configuration ---

  [[nodiscard]] auto GetLastCookedOutput() const -> std::string;
  auto SetLastCookedOutput(const std::string& path) -> void;

  [[nodiscard]] auto GetExplorerSettings() const -> ContentExplorerSettings;
  auto SetExplorerSettings(const ContentExplorerSettings& settings) -> void;

  [[nodiscard]] auto GetImportOptions() const -> content::import::ImportOptions;
  auto SetImportOptions(const content::import::ImportOptions& options) -> void;

  [[nodiscard]] auto GetTextureTuning() const
    -> content::import::ImportOptions::TextureTuning;
  auto SetTextureTuning(
    const content::import::ImportOptions::TextureTuning& tuning) -> void;

  [[nodiscard]] auto GetServiceConfig() const
    -> content::import::AsyncImportService::Config;
  auto SetServiceConfig(
    const content::import::AsyncImportService::Config& config) -> void;
  auto RestartImportService() -> void;

  [[nodiscard]] auto GetLayout() const -> content::import::LooseCookedLayout;
  auto SetLayout(const content::import::LooseCookedLayout& layout) -> void;

  // --- Diagnostics ---

  auto AddDiagnosticMarker(const std::string& label, bool is_start) -> void;

  [[nodiscard]] auto GetDiagnostics() const
    -> const std::vector<content::import::ImportDiagnostic>&;
  auto ClearDiagnostics() -> void;

  // --- Utils ---
  auto ForceTrimCaches() -> void;
  //! Register callback for force-trim requests.
  auto SetOnForceTrim(std::function<void()> callback) -> void;

  //! Access to the file browser service for drawing in UI context.
  [[nodiscard]] auto GetFileBrowser() const -> observer_ptr<FileBrowserService>;

  //! Lifecycle management: call periodically
  auto Update() -> void;

private:
  struct ImportJobState {
    std::atomic_bool is_importing { false };
    std::atomic_bool cancel_requested { false };
    std::atomic_bool completion_ready { false };
    std::string current_path;
    content::import::ImportJobId job_id { content::import::kInvalidJobId };

    mutable std::mutex progress_mutex;
    content::import::ProgressEvent progress {};
    std::vector<content::import::ImportDiagnostic> diagnostics;

    std::mutex completion_mutex;
    std::optional<content::import::ImportReport> completion_report;
    std::string completion_error;

    // --- Scene Loading Progress ---
    std::atomic_bool is_scene_loading { false };
    std::atomic<float> scene_load_progress { 0.0f };
    std::string scene_load_message;
    std::optional<data::AssetKey> scene_load_key;
    std::optional<std::chrono::steady_clock::time_point> scene_load_finish_time;
  };

  auto OnImportComplete(content::import::ImportJobId job_id,
    const content::import::ImportReport& report) -> void;
  auto OnImportProgress(const content::import::ProgressEvent& progress) -> void;
  [[nodiscard]] auto ResolveSceneLabel(
    const std::optional<data::AssetKey>& key) const -> std::string;

  enum class BrowseMode {
    kNone,
    kModelRoot,
    kSourceFile,
    kPakFile,
    kIndexFile
  };
  BrowseMode browse_mode_ { BrowseMode::kNone };

  observer_ptr<ContentSettingsService> settings_;
  observer_ptr<FileBrowserService> file_browser_;

  std::unique_ptr<content::import::AsyncImportService> import_service_;
  content::import::AsyncImportService::Config service_config_ {};
  ImportJobState import_state_;

  mutable std::mutex data_mutex_;
  std::vector<ContentSource> cached_sources_;
  std::vector<std::filesystem::path> discovered_paks_;
  std::vector<std::filesystem::path> loaded_paks_;
  std::vector<std::filesystem::path> loaded_indices_;
  std::vector<SceneEntry> available_scenes_;
  std::unordered_map<data::AssetKey, SceneEntry> scenes_map_;
  std::uint64_t settings_epoch_ { 0 };

  //! Callbacks to update engine state
  std::function<void(const std::filesystem::path&)> on_pak_mounted_;
  std::function<void(const std::filesystem::path&)> on_index_loaded_;
  std::function<void(const data::AssetKey&)> on_scene_load_requested_;
  std::function<void()> on_scene_load_cancel_requested_;
  std::function<void()> on_force_trim_;
};

} // namespace oxygen::examples::ui

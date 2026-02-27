//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>

#include "DemoShell/Runtime/PathNormalization.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentVm.h"

#include <ranges>

namespace oxygen::examples::ui {

namespace {

  auto NormalizePathForKey(const std::filesystem::path& path) -> std::string
  {
    return runtime::NormalizePath(path).string();
  }

  auto IsPathUnderRoot(const std::filesystem::path& path,
    const std::filesystem::path& root) -> bool
  {
    if (path.empty() || root.empty()) {
      return false;
    }

    const auto normalized_path = runtime::NormalizePath(path);
    const auto normalized_root = runtime::NormalizePath(root);

    auto path_it = normalized_path.begin();
    auto root_it = normalized_root.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++path_it) {
      if (path_it == normalized_path.end() || *path_it != *root_it) {
        return false;
      }
    }
    return true;
  }

  auto ShouldOverrideCookedRoot(const std::filesystem::path& current_root)
    -> bool
  {
    if (current_root.empty()) {
      return true;
    }

    std::error_code ec;
    const auto temp_root = std::filesystem::temp_directory_path(ec);
    if (ec) {
      return false;
    }

    return IsPathUnderRoot(current_root, temp_root);
  }

} // namespace

auto ContentVm::MakeSceneEntryKey(const SceneEntry& entry) -> SceneEntryKey
{
  return SceneEntryKey {
    .key = entry.key,
    .source_kind = entry.source.kind,
    .source_key = NormalizePathForKey(entry.source.path),
  };
}

auto ContentVm::RebuildSceneList(
  const std::unordered_map<SceneEntryKey, SceneEntry, SceneEntryKeyHash,
    SceneEntryKeyEq>& entries,
  std::vector<SceneEntry>& out) -> void
{
  out.clear();
  out.reserve(entries.size());
  for (const auto& entry : entries | std::views::values) {
    out.push_back(entry);
  }

  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    if (a.name != b.name) {
      return a.name < b.name;
    }
    if (a.source.kind != b.source.kind) {
      return static_cast<int>(a.source.kind) < static_cast<int>(b.source.kind);
    }
    return a.source.path.string() < b.source.path.string();
  });
}

ContentVm::ContentVm(observer_ptr<ContentSettingsService> settings_service,
  observer_ptr<FileBrowserService> file_browser_service,
  observer_ptr<content::IAssetLoader> asset_loader)
  : settings_(settings_service)
  , file_browser_(file_browser_service)
  , asset_loader_(asset_loader)
{
  // Default config for import service
  service_config_.thread_pool_size = 35;
  service_config_.max_in_flight_jobs = 35;
  service_config_.concurrency.texture.workers = 12;
  service_config_.concurrency.texture.queue_capacity = 64;
  service_config_.concurrency.buffer.workers = 2;
  service_config_.concurrency.buffer.queue_capacity = 64;
  service_config_.concurrency.material.workers = 4;
  service_config_.concurrency.material.queue_capacity = 64;
  service_config_.concurrency.mesh_build.workers = 12;
  service_config_.concurrency.mesh_build.queue_capacity = 128;
  service_config_.concurrency.geometry.workers = 8;
  service_config_.concurrency.geometry.queue_capacity = 64;
  service_config_.concurrency.scene.workers = 1;
  service_config_.concurrency.scene.queue_capacity = 8;

  import_service_
    = std::make_unique<content::import::AsyncImportService>(service_config_);

  // Initialize default model root from file browser if current settings are
  // empty
  if (file_browser_) {
    auto s = settings_->GetExplorerSettings();
    if (s.model_root.empty()) {
      const auto defaults = file_browser_->GetContentRoots();
      s.model_root = defaults.content_root;
      LOG_F(INFO, "ContentVm: Initializing default model root to: '{}'",
        s.model_root.string());
      settings_->SetExplorerSettings(s);
    }

    const auto cooked_root = settings_->GetLastCookedOutputDirectory();
    if (ShouldOverrideCookedRoot(cooked_root)) {
      const auto defaults = file_browser_->GetContentRoots();
      settings_->SetLastCookedOutputDirectory(defaults.cooked_root.string());
      LOG_F(INFO, "ContentVm: Initializing cooked output to: '{}'",
        defaults.cooked_root.string());
    }
  }

  RefreshSources();
  RefreshLibrary();
}

ContentVm::~ContentVm()
{
  if (import_service_) {
    import_service_->Stop();
  }
}

auto ContentVm::Update() -> void
{
  // Handle File Browser results
  if (file_browser_) {
    if (browse_request_id_ != 0) {
      const auto result = file_browser_->ConsumeResult(browse_request_id_);
      if (result) {
        if (result->kind == FileBrowserService::ResultKind::kSelected) {
          LOG_F(INFO, "ContentVm: FileBrowser selection '{}' (mode={})",
            result->path.string(), static_cast<int>(browse_mode_));
          if (browse_mode_ == BrowseMode::kModelRoot) {
            auto s = settings_->GetExplorerSettings();
            s.model_root = result->path;
            settings_->SetExplorerSettings(s);
            RefreshSources();
          } else if (browse_mode_ == BrowseMode::kSourceFile) {
            StartImport(result->path);
          } else if (browse_mode_ == BrowseMode::kPakFile) {
            MountPak(result->path);
          } else if (browse_mode_ == BrowseMode::kIndexFile) {
            LoadIndex(result->path);
          } else {
            LOG_F(
              WARNING, "ContentVm: FileBrowser selection ignored (mode=None)");
          }
        } else if (result->kind == FileBrowserService::ResultKind::kCanceled) {
          if (browse_mode_ != BrowseMode::kNone) {
            LOG_F(INFO, "ContentVm: FileBrowser closed without selection");
          }
        }
        browse_mode_ = BrowseMode::kNone;
        browse_request_id_ = 0;
      }
    }
  }

  // Auto-dismiss scene loading message after 10 seconds
  if (import_state_.scene_load_finish_time) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(
          now - *import_state_.scene_load_finish_time)
          .count()
      >= 5) {
      import_state_.scene_load_finish_time.reset();
      import_state_.scene_load_message.clear();
    }
  }

  if (!import_state_.completion_ready.load(std::memory_order_relaxed)) {
    TryResolvePendingSceneSelection();
    return;
  }

  std::optional<content::import::ImportReport> report;
  std::string completion_error;
  {
    std::lock_guard lock(import_state_.completion_mutex);
    report = import_state_.completion_report;
    completion_error = import_state_.completion_error;
    import_state_.completion_report.reset();
    import_state_.completion_error.clear();
  }

  const auto import_path = import_state_.current_path;

  import_state_.completion_ready.store(false, std::memory_order_relaxed);
  import_state_.cancel_requested.store(false, std::memory_order_relaxed);
  import_state_.current_path.clear();
  import_state_.job_id = content::import::kInvalidJobId;
  import_state_.is_importing.store(false, std::memory_order_relaxed);

  if (!completion_error.empty()) {
    LOG_F(ERROR, "ContentVm: Import failed with error: {}", completion_error);
    AddDiagnosticMarker("Import: " + import_path + " (Failed)", false);
  } else if (report) {
    LOG_F(INFO,
      "ContentVm: Import job completed. Success: {}. Materials: {}. Geometry: "
      "{}. Scenes: {}.",
      report->success, report->materials_written, report->geometry_written,
      report->scenes_written);

    std::string label = "Import: " + import_path;
    AddDiagnosticMarker(
      label + (report->success ? " (Completed)" : " (Failed)"), false);

    if (report->success) {
      // Logic to extract scenes from the newly imported content
      const auto layout = settings_->GetDefaultLayout();
      const auto index_path
        = report->cooked_root / std::filesystem::path(layout.index_file_name);

      try {
        LOG_F(INFO, "ContentVm: Inspecting imported index: '{}'",
          index_path.string());
        content::lc::Inspection inspection;
        inspection.LoadFromFile(index_path);

        int scene_count = 0;
        std::vector<SceneEntry> imported_scenes;
        std::unordered_map<std::string, SceneEntry> scene_by_descriptor;
        const SceneSource source { .kind = SceneSourceKind::kLooseIndex,
          .path = index_path };
        {
          std::lock_guard data_lock(data_mutex_);
          for (const auto& asset : inspection.Assets()) {
            if (asset.asset_type
              == static_cast<uint8_t>(data::AssetType::kScene)) {
              const SceneEntry entry {
                .name = asset.virtual_path,
                .key = asset.key,
                .source = source,
              };
              scenes_map_[MakeSceneEntryKey(entry)] = entry;
              scene_count++;
              scene_by_descriptor.emplace(asset.descriptor_relpath, entry);
            }
          }

          RebuildSceneList(scenes_map_, available_scenes_);
        }
        LOG_F(INFO, "ContentVm: Discovered {} new scenes in imported content.",
          scene_count);

        // Notify engine about the new index so it can be mounted for loading
        {
          std::lock_guard data_lock(data_mutex_);
          if (std::find(
                loaded_indices_.begin(), loaded_indices_.end(), index_path)
            == loaded_indices_.end()) {
            loaded_indices_.push_back(index_path);
          }
        }
        if (on_index_loaded_) {
          on_index_loaded_(index_path);
        }

        if (settings_->GetExplorerSettings().auto_load_on_import
          && scene_count > 0) {
          for (const auto& output : report->outputs) {
            const auto it = scene_by_descriptor.find(output.path);
            if (it != scene_by_descriptor.end()) {
              imported_scenes.push_back(it->second);
            }
          }

          if (!imported_scenes.empty()) {
            const auto& imported_scene = imported_scenes.back();
            LOG_F(INFO, "ContentVm: Auto-loading imported scene: '{}'",
              imported_scene.name);
            RequestSceneLoad(imported_scene);
          } else if (!available_scenes_.empty()) {
            const auto& fallback_scene = available_scenes_.back();
            LOG_F(INFO, "ContentVm: Auto-loading latest scene: '{}'",
              fallback_scene.name);
            RequestSceneLoad(fallback_scene);
          }
        }
      } catch (const std::exception& ex) {
        LOG_F(WARNING, "ContentVm: Failed to inspect imported assets: {}",
          ex.what());
      }
    }
  }
}

auto ContentVm::StartImport(const std::filesystem::path& source_path) -> void
{
  if (IsImportInProgress())
    return;

  import_state_.current_path = source_path.string();
  import_state_.is_importing.store(true, std::memory_order_relaxed);
  import_state_.cancel_requested.store(false, std::memory_order_relaxed);
  import_state_.completion_ready.store(false, std::memory_order_relaxed);

  AddDiagnosticMarker("Import: " + source_path.string() + " (Started)", true);

  LOG_F(INFO, "ContentVm: Starting import of '{}'", source_path.string());

  content::import::ImportRequest request {};
  request.source_path = source_path;
  request.cooked_root = settings_->GetLastCookedOutputDirectory();
  request.options = settings_->GetImportOptions();
  request.options.texture_tuning = settings_->GetTextureTuning();
  request.loose_cooked_layout = settings_->GetDefaultLayout();

  // Use default naming strategy for now (can be expanded in settings)
  request.options.naming_strategy
    = std::make_shared<content::import::NormalizeNamingStrategy>();

  auto on_complete = [this](content::import::ImportJobId id,
                       const content::import::ImportReport& rep) {
    this->OnImportComplete(id, rep);
  };

  auto on_progress = [this](const content::import::ProgressEvent& prog) {
    this->OnImportProgress(prog);
  };

  auto job_id
    = import_service_->SubmitImport(request, on_complete, on_progress);
  if (job_id) {
    import_state_.job_id = *job_id;
  } else {
    import_state_.is_importing.store(false);
    LOG_F(ERROR, "ContentVm: Service rejected import request");
  }
}

auto ContentVm::CancelActiveImport() -> void
{
  if (!IsImportInProgress())
    return;
  import_state_.cancel_requested = true;
  import_service_->CancelJob(import_state_.job_id);
}

auto ContentVm::BrowseForModelRoot() -> void
{
  if (!file_browser_)
    return;
  auto config
    = MakeModelDirectoryBrowserConfig(file_browser_->GetContentRoots());
  const auto s = settings_->GetExplorerSettings();
  if (!s.model_root.empty()) {
    config.initial_directory = s.model_root;
  }
  browse_request_id_ = file_browser_->Open(config);
  browse_mode_ = BrowseMode::kModelRoot;
}

auto ContentVm::BrowseForSourceFile() -> void
{
  if (!file_browser_)
    return;
  auto config = MakeModelFileBrowserConfig(file_browser_->GetContentRoots());
  const auto s = settings_->GetExplorerSettings();
  if (!s.model_root.empty()) {
    config.initial_directory = s.model_root;
  }
  browse_request_id_ = file_browser_->Open(config);
  browse_mode_ = BrowseMode::kSourceFile;
}

auto ContentVm::IsImportInProgress() const -> bool
{
  return import_state_.is_importing.load();
}
auto ContentVm::GetActiveImportPath() const -> std::string
{
  return import_state_.current_path;
}

auto ContentVm::GetActiveImportProgress() const -> float
{
  std::lock_guard lock(import_state_.progress_mutex);
  return import_state_.progress.header.overall_progress;
}

auto ContentVm::GetActiveImportMessage() const -> std::string
{
  std::lock_guard lock(import_state_.progress_mutex);
  return import_state_.progress.header.message;
}

auto ContentVm::RefreshSources() -> void
{
  const auto s = settings_->GetExplorerSettings();
  std::vector<ContentSource> sources;

  LOG_F(INFO, "ContentVm: Refreshing sources. Model root: '{}'",
    s.model_root.string());

  std::error_code ec;
  if (s.model_root.empty()) {
    LOG_F(WARNING, "ContentVm: Model root is empty. No sources will be found.");
  } else if (!std::filesystem::exists(s.model_root, ec)) {
    LOG_F(WARNING, "ContentVm: Model root does not exist: '{}' (error: {})",
      s.model_root.string(), ec.message());
  } else if (!std::filesystem::is_directory(s.model_root, ec)) {
    LOG_F(WARNING, "ContentVm: Model root is not a directory: '{}' (error: {})",
      s.model_root.string(), ec.message());
  } else {
    LOG_F(INFO, "ContentVm: Scanning model root for FBX/GLB/GLTF files...");
    for (const auto& entry :
      std::filesystem::recursive_directory_iterator(s.model_root, ec)) {
      if (ec) {
        LOG_F(ERROR, "ContentVm: Error during iteration at '{}': {}",
          entry.path().string(), ec.message());
        ec.clear(); // Clear so we can potentially continue or at least not fail
                    // the whole function
        continue;
      }
      if (!entry.is_regular_file())
        continue;

      const auto ext = entry.path().extension().string();
      if (s.include_fbx && ext == ".fbx") {
        sources.push_back(
          { entry.path(), content::import::ImportFormat::kFbx });
        DLOG_F(INFO, "ContentVm: Found FBX: {}", entry.path().string());
      } else if (s.include_glb && ext == ".glb") {
        sources.push_back(
          { entry.path(), content::import::ImportFormat::kGltf });
        DLOG_F(INFO, "ContentVm: Found GLB: {}", entry.path().string());
      } else if (s.include_gltf && ext == ".gltf") {
        sources.push_back(
          { entry.path(), content::import::ImportFormat::kGltf });
        DLOG_F(INFO, "ContentVm: Found GLTF: {}", entry.path().string());
      }
    }
    if (ec) {
      LOG_F(ERROR, "ContentVm: Iteration ended with error: {}", ec.message());
    }
    LOG_F(
      INFO, "ContentVm: Discovery complete. Found {} sources.", sources.size());
  }

  std::lock_guard lock(data_mutex_);
  cached_sources_ = std::move(sources);
}

auto ContentVm::GetSources() const -> const std::vector<ContentSource>&
{
  return cached_sources_;
}

auto ContentVm::RefreshLibrary() -> void
{
  const auto roots = file_browser_->GetContentRoots();
  std::vector<std::filesystem::path> paks;

  std::error_code ec;
  if (std::filesystem::is_directory(roots.pak_directory, ec)) {
    for (const auto& entry :
      std::filesystem::directory_iterator(roots.pak_directory, ec)) {
      if (entry.path().extension() == ".pak") {
        paks.push_back(entry.path());
      }
    }
  }

  std::unordered_map<SceneEntryKey, SceneEntry, SceneEntryKeyHash,
    SceneEntryKeyEq>
    runtime_scenes {};
  std::vector<std::filesystem::path> runtime_loaded_paks;
  std::vector<std::filesystem::path> runtime_loaded_indices;
  if (asset_loader_) {
    for (const auto& mounted_source :
      asset_loader_->EnumerateMountedSources()) {
      if (mounted_source.source_kind
        == content::IAssetLoader::ContentSourceKind::kPak) {
        runtime_loaded_paks.push_back(mounted_source.source_path);
      } else {
        runtime_loaded_indices.push_back(
          mounted_source.source_path / "container.index.bin");
      }
    }
    for (const auto& mounted_scene : asset_loader_->EnumerateMountedScenes()) {
      std::filesystem::path scene_source_path = mounted_scene.source_path;
      if (mounted_scene.source_kind
        == content::IAssetLoader::ContentSourceKind::kLooseCooked) {
        scene_source_path /= "container.index.bin";
      }
      const SceneSource source {
        .kind = mounted_scene.source_kind
            == content::IAssetLoader::ContentSourceKind::kPak
          ? SceneSourceKind::kPak
          : SceneSourceKind::kLooseIndex,
        .path = std::move(scene_source_path),
      };
      std::string scene_name;
      if (!mounted_scene.virtual_path.empty()) {
        scene_name = mounted_scene.virtual_path;
      } else if (!mounted_scene.display_name.empty()) {
        scene_name = mounted_scene.display_name;
      } else {
        scene_name = "Scene (No Name)";
      }
      const SceneEntry scene_entry {
        .name = std::move(scene_name),
        .key = mounted_scene.scene_key,
        .source = source,
      };
      runtime_scenes[MakeSceneEntryKey(scene_entry)] = scene_entry;
    }
  }

  {
    std::lock_guard lock(data_mutex_);
    discovered_paks_ = std::move(paks);
    loaded_paks_ = std::move(runtime_loaded_paks);
    loaded_indices_ = std::move(runtime_loaded_indices);
    scenes_map_ = std::move(runtime_scenes);
    RebuildSceneList(scenes_map_, available_scenes_);
  }
  TryResolvePendingSceneSelection();
}

auto ContentVm::SetOnPakMounted(
  std::function<void(const std::filesystem::path&)> callback) -> void
{
  on_pak_mounted_ = std::move(callback);
}
auto ContentVm::SetOnIndexLoaded(
  std::function<void(const std::filesystem::path&)> callback) -> void
{
  on_index_loaded_ = std::move(callback);
}

auto ContentVm::MountPak(const std::filesystem::path& path) -> void
{
  const auto normalized = runtime::NormalizePath(path);
  if (on_pak_mounted_) {
    on_pak_mounted_(normalized);
  } else if (asset_loader_) {
    asset_loader_->AddPakFile(normalized);
    RefreshLibrary();
    PersistMountedSources();
  }
}

auto ContentVm::LoadIndex(const std::filesystem::path& path) -> void
{
  try {
    std::error_code ec;
    const bool is_dir = std::filesystem::is_directory(path, ec);
    if (ec) {
      LOG_F(WARNING,
        "ContentVm: Failed to stat index path '{}': {} (treating as file)",
        path.string(), ec.message());
    }

    std::filesystem::path index_path = path;
    if (!ec && is_dir) {
      LOG_F(INFO, "ContentVm: Loading loose cooked root '{}'", path.string());
      index_path = path / "container.index.bin";
    } else {
      LOG_F(INFO, "ContentVm: Loading loose cooked index '{}'", path.string());
    }
    if (on_index_loaded_) {
      on_index_loaded_(index_path);
    } else if (asset_loader_) {
      const auto loose_root = (!ec && is_dir) ? path : path.parent_path();
      asset_loader_->AddLooseCookedRoot(loose_root);
      RefreshLibrary();
      PersistMountedSources();
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "ContentVm: Failed to load index '{}': {}", path.string(),
      ex.what());
  }
}

auto ContentVm::UnloadAllLibrary() -> void
{
  std::lock_guard lock(data_mutex_);
  loaded_paks_.clear();
  loaded_indices_.clear();
  scenes_map_.clear();
  available_scenes_.clear();
  if (settings_) {
    settings_->SetMountedPakPaths({});
    settings_->SetMountedIndexPaths({});
    settings_->SetActiveSceneSelection(std::nullopt);
  }
}

auto ContentVm::RestorePersistedLibraryState() -> void
{
  if (persisted_library_state_restored_) {
    return;
  }

  if (!settings_) {
    return;
  }

  persisted_library_state_restored_ = true;

  for (const auto& pak_path : settings_->GetMountedPakPaths()) {
    if (!pak_path.empty()) {
      MountPak(pak_path);
    }
  }
  for (const auto& index_path : settings_->GetMountedIndexPaths()) {
    if (!index_path.empty()) {
      LoadIndex(index_path);
    }
  }

  RefreshLibrary();

  const auto selection = settings_->GetActiveSceneSelection();
  if (!selection.has_value()) {
    return;
  }
  pending_scene_selection_restore_ = selection;
  TryResolvePendingSceneSelection();
}

auto ContentVm::PersistLibraryState() -> void { PersistMountedSources(); }

auto ContentVm::GetDiscoveredPaks() const
  -> const std::vector<std::filesystem::path>&
{
  return discovered_paks_;
}
auto ContentVm::GetLoadedPaks() const
  -> const std::vector<std::filesystem::path>&
{
  return loaded_paks_;
}
auto ContentVm::GetLoadedIndices() const
  -> const std::vector<std::filesystem::path>&
{
  return loaded_indices_;
}

auto ContentVm::BrowseForPak() -> void
{
  if (!file_browser_)
    return;
  auto config = MakePakFileBrowserConfig(file_browser_->GetContentRoots());
  browse_request_id_ = file_browser_->Open(config);
  browse_mode_ = BrowseMode::kPakFile;
}

auto ContentVm::BrowseForIndex() -> void
{
  if (!file_browser_)
    return;
  LOG_F(INFO, "ContentVm: Opening file browser for loose cooked index");
  auto config
    = MakeLooseCookedIndexBrowserConfig(file_browser_->GetContentRoots());
  browse_request_id_ = file_browser_->Open(config);
  browse_mode_ = BrowseMode::kIndexFile;
}

auto ContentVm::GetAvailableScenes() const -> const std::vector<SceneEntry>&
{
  return available_scenes_;
}

auto ContentVm::RequestSceneLoad(const SceneEntry& entry) -> void
{
  if (IsSceneLoading()) {
    LOG_F(WARNING, "ContentVm: Scene load already in progress");
    return;
  }
  if (on_scene_load_requested_) {
    PersistActiveSceneSelection(entry);
    const auto scene_name = entry.name;
    {
      std::lock_guard lock(import_state_.progress_mutex);
      import_state_.is_scene_loading = true;
      import_state_.scene_load_progress = 0.0F;
      import_state_.scene_load_message = "Loading scene: " + scene_name;
      import_state_.scene_load_label = scene_name;
      import_state_.scene_load_key = entry.key;
      import_state_.scene_load_finish_time.reset();
    }

    AddDiagnosticMarker("Load Scene: " + scene_name + " (Started)", true);
    on_scene_load_requested_(entry);
  }
}

auto ContentVm::PersistMountedSources() -> void
{
  if (!settings_) {
    return;
  }

  std::vector<std::filesystem::path> paks;
  std::vector<std::filesystem::path> indices;
  if (asset_loader_) {
    for (const auto& mounted_source :
      asset_loader_->EnumerateMountedSources()) {
      if (mounted_source.source_kind
        == content::IAssetLoader::ContentSourceKind::kPak) {
        paks.push_back(mounted_source.source_path);
      } else {
        indices.push_back(mounted_source.source_path / "container.index.bin");
      }
    }
    {
      std::lock_guard lock(data_mutex_);
      loaded_paks_ = paks;
      loaded_indices_ = indices;
    }
  } else {
    std::lock_guard lock(data_mutex_);
    paks = loaded_paks_;
    indices = loaded_indices_;
  }
  settings_->SetMountedPakPaths(paks);
  settings_->SetMountedIndexPaths(indices);
}

auto ContentVm::PersistActiveSceneSelection(const SceneEntry& entry) -> void
{
  if (!settings_) {
    return;
  }

  settings_->SetActiveSceneSelection(ContentActiveSceneSelection {
    .scene_name = entry.name,
    .scene_key = nostd::to_string(entry.key),
    .source_path = entry.source.path,
    .source_is_pak = entry.source.kind == SceneSourceKind::kPak,
  });
}

auto ContentVm::TryResolvePendingSceneSelection() -> void
{
  if (!pending_scene_selection_restore_.has_value()) {
    return;
  }

  const auto& selection = *pending_scene_selection_restore_;
  const auto desired_kind = selection.source_is_pak
    ? SceneSourceKind::kPak
    : SceneSourceKind::kLooseIndex;
  const auto desired_path = NormalizePathForKey(selection.source_path);

  std::optional<SceneEntry> match;
  {
    std::lock_guard lock(data_mutex_);
    for (const auto& scene : available_scenes_) {
      if (scene.source.kind != desired_kind) {
        continue;
      }
      if (NormalizePathForKey(scene.source.path) != desired_path) {
        continue;
      }
      if (!selection.scene_key.empty()
        && nostd::to_string(scene.key) != selection.scene_key) {
        continue;
      }
      if (!selection.scene_name.empty() && scene.name != selection.scene_name
        && !selection.scene_key.empty()) {
        continue;
      }
      match = scene;
      break;
    }
  }

  if (!match.has_value()) {
    return;
  }
  pending_scene_selection_restore_.reset();
  RequestSceneLoad(*match);
}

auto ContentVm::IsSceneLoading() const -> bool
{
  return import_state_.is_scene_loading;
}
auto ContentVm::GetSceneLoadProgress() const -> float
{
  return import_state_.scene_load_progress;
}
auto ContentVm::GetSceneLoadMessage() const -> std::string
{
  std::lock_guard lock(import_state_.progress_mutex);
  return import_state_.scene_load_message;
}
auto ContentVm::ShouldShowSceneLoadProgress() const -> bool
{
  return import_state_.is_scene_loading
    || !import_state_.scene_load_message.empty();
}

auto ContentVm::CancelSceneLoad() -> void
{
  if (!IsSceneLoading()) {
    return;
  }

  if (on_scene_load_cancel_requested_) {
    on_scene_load_cancel_requested_();
  }

  std::optional<data::AssetKey> scene_key;
  std::string scene_label;
  {
    std::lock_guard lock(import_state_.progress_mutex);
    scene_key = import_state_.scene_load_key;
    scene_label = import_state_.scene_load_label;
  }
  const auto scene_name
    = scene_label.empty() ? ResolveSceneLabel(scene_key) : scene_label;
  {
    std::lock_guard lock(import_state_.progress_mutex);
    import_state_.is_scene_loading = false;
    import_state_.scene_load_progress = 1.0F;
    import_state_.scene_load_message = "Cancelled scene load: " + scene_name;
    import_state_.scene_load_key.reset();
    import_state_.scene_load_label.clear();
    import_state_.scene_load_finish_time = std::chrono::steady_clock::now();
  }
  AddDiagnosticMarker("Load Scene: " + scene_name + " (Cancelled)", false);
}

auto ContentVm::NotifySceneLoadCompleted(
  const data::AssetKey& key, bool success) -> void
{
  if (!IsSceneLoading()) {
    return;
  }

  std::optional<data::AssetKey> active_key;
  std::string scene_label;
  {
    std::lock_guard lock(import_state_.progress_mutex);
    active_key = import_state_.scene_load_key;
    scene_label = import_state_.scene_load_label;
  }
  if (active_key.has_value() && *active_key != key) {
    LOG_F(WARNING, "ContentVm: Ignoring scene completion for stale key");
    return;
  }

  const auto scene_name
    = scene_label.empty() ? ResolveSceneLabel(key) : scene_label;
  {
    std::lock_guard lock(import_state_.progress_mutex);
    import_state_.is_scene_loading = false;
    import_state_.scene_load_progress = 1.0F;
    import_state_.scene_load_message = success
      ? "Loaded scene: " + scene_name
      : "Failed to load scene: " + scene_name;
    import_state_.scene_load_key.reset();
    import_state_.scene_load_label.clear();
    import_state_.scene_load_finish_time = std::chrono::steady_clock::now();
  }
  AddDiagnosticMarker(
    "Load Scene: " + scene_name + (success ? " (Completed)" : " (Failed)"),
    false);
}

auto ContentVm::SetOnSceneLoadRequested(
  std::function<void(const SceneEntry&)> callback) -> void
{
  on_scene_load_requested_ = std::move(callback);
}

auto ContentVm::SetOnSceneLoadCancelRequested(std::function<void()> callback)
  -> void
{
  on_scene_load_cancel_requested_ = std::move(callback);
}

auto ContentVm::GetLastCookedOutput() const -> std::string
{
  return settings_->GetLastCookedOutputDirectory();
}
auto ContentVm::SetLastCookedOutput(const std::string& path) -> void
{
  settings_->SetLastCookedOutputDirectory(path);
}

auto ContentVm::GetExplorerSettings() const -> ContentExplorerSettings
{
  return settings_->GetExplorerSettings();
}
auto ContentVm::SetExplorerSettings(const ContentExplorerSettings& settings)
  -> void
{
  settings_->SetExplorerSettings(settings);
}

auto ContentVm::GetImportOptions() const -> content::import::ImportOptions
{
  return settings_->GetImportOptions();
}
auto ContentVm::SetImportOptions(const content::import::ImportOptions& options)
  -> void
{
  settings_->SetImportOptions(options);
}

auto ContentVm::GetTextureTuning() const
  -> content::import::ImportOptions::TextureTuning
{
  return settings_->GetTextureTuning();
}
auto ContentVm::SetTextureTuning(
  const content::import::ImportOptions::TextureTuning& tuning) -> void
{
  settings_->SetTextureTuning(tuning);
}

auto ContentVm::GetServiceConfig() const
  -> content::import::AsyncImportService::Config
{
  return service_config_;
}
auto ContentVm::SetServiceConfig(
  const content::import::AsyncImportService::Config& config) -> void
{
  service_config_ = config;
}

auto ContentVm::RestartImportService() -> void
{
  if (import_service_) {
    import_service_->Stop();
  }
  import_service_
    = std::make_unique<content::import::AsyncImportService>(service_config_);
}

auto ContentVm::GetLayout() const -> content::import::LooseCookedLayout
{
  return settings_->GetDefaultLayout();
}
auto ContentVm::SetLayout(const content::import::LooseCookedLayout& layout)
  -> void
{
  settings_->SetDefaultLayout(layout);
}

auto ContentVm::AddDiagnosticMarker(const std::string& label, bool is_start)
  -> void
{
  content::import::ImportDiagnostic diag;
  diag.severity = content::import::ImportSeverity::kInfo;
  diag.code = is_start ? "marker.start" : "marker.end";
  diag.message = "--- " + label + " ---";

  std::lock_guard lock(import_state_.progress_mutex);
  import_state_.diagnostics.push_back(std::move(diag));
}

auto ContentVm::GetDiagnostics() const
  -> const std::vector<content::import::ImportDiagnostic>&
{
  std::lock_guard lock(import_state_.progress_mutex);
  return import_state_.diagnostics;
}

auto ContentVm::ClearDiagnostics() -> void
{
  std::lock_guard lock(import_state_.progress_mutex);
  import_state_.diagnostics.clear();
}

auto ContentVm::ForceTrimCaches() -> void
{
  LOG_F(INFO, "ContentVm: Force trimming asset cache.");
  if (on_force_trim_) {
    on_force_trim_();
  }
}

auto ContentVm::SetOnForceTrim(std::function<void()> callback) -> void
{
  on_force_trim_ = std::move(callback);
}

auto ContentVm::OnImportComplete(content::import::ImportJobId job_id,
  const content::import::ImportReport& report) -> void
{
  if (import_state_.job_id != job_id)
    return;

  LOG_F(INFO, "ContentVm: OnImportComplete for job {}", job_id.get());
  {
    std::lock_guard lock(import_state_.completion_mutex);
    import_state_.completion_report = report;
  }
  import_state_.completion_ready.store(true);
}

auto ContentVm::OnImportProgress(const content::import::ProgressEvent& progress)
  -> void
{
  std::lock_guard lock(import_state_.progress_mutex);
  import_state_.progress = progress;
  if (!progress.header.new_diagnostics.empty()) {
    import_state_.diagnostics.insert(import_state_.diagnostics.end(),
      progress.header.new_diagnostics.begin(),
      progress.header.new_diagnostics.end());
  }
}

auto ContentVm::GetFileBrowser() const -> observer_ptr<FileBrowserService>
{
  return file_browser_;
}

auto ContentVm::ResolveSceneLabel(
  const std::optional<data::AssetKey>& key) const -> std::string
{
  if (!key.has_value()) {
    return "Unknown Scene";
  }

  {
    std::lock_guard lock(data_mutex_);
    for (const auto& [entry_key, entry] : scenes_map_) {
      if (entry_key.key == *key) {
        return entry.name;
      }
    }
  }

  return data::to_string(*key);
}

} // namespace oxygen::examples::ui

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DemoShell/Runtime/PathNormalization.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentVm.h"

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>

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

  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) -> auto {
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
          } else if (browse_mode_ == BrowseMode::kLibraryFile) {
            if (result->path.filename().string().ends_with(".import.json")) {
              LoadImportRecord(result->path);
            } else {
              LoadIndex(result->path);
            }
          } else {
            LOG_F(
              WARNING, "ContentVm: FileBrowser selection ignored (mode=None)");
          }
        } else if ((result->kind == FileBrowserService::ResultKind::kCanceled)
          && (browse_mode_ != BrowseMode::kNone)) {
          LOG_F(INFO, "ContentVm: FileBrowser closed without selection");
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
        const SceneSource source {
          .kind = SceneSourceKind::kLooseIndex,
          .path = index_path,
        };
        {
          std::lock_guard data_lock(data_mutex_);
          if (report->retained_record_path
            && std::ranges::find(
                 loaded_import_records_, *report->retained_record_path)
              == loaded_import_records_.end()) {
            loaded_import_records_.push_back(*report->retained_record_path);
            settings_->SetMountedImportRecords(loaded_import_records_);
          }
          if (report->previous_cooked_root) {
            const auto previous_index = runtime::NormalizePath(
              *report->previous_cooked_root / "container.index.bin");
            std::erase_if(scenes_map_, [&](const auto& entry) -> auto {
              return entry.second.source.kind == SceneSourceKind::kLooseIndex
                && runtime::NormalizePath(entry.second.source.path)
                == previous_index;
            });
            std::erase(loaded_indices_, previous_index);
          }
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
        if (report->retained_record_path) {
          MountGeneration(GenerationPublication {
            .cooked_root = report->cooked_root,
            .previous_root = report->previous_cooked_root,
          });
        } else if (on_index_loaded_) {
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
  if (IsImportInProgress()) {
    return;
  }

  import_state_.current_path = source_path.string();
  import_state_.is_importing.store(true, std::memory_order_relaxed);
  import_state_.cancel_requested.store(false, std::memory_order_relaxed);
  import_state_.completion_ready.store(false, std::memory_order_relaxed);

  AddDiagnosticMarker("Import: " + source_path.string() + " (Started)", true);

  LOG_F(INFO, "ContentVm: Starting import of '{}'", source_path.string());

  try {
    if (!file_browser_) {
      throw std::logic_error("App-owned Content roots are unavailable");
    }
    auto options = settings_->GetImportOptions();
    options.texture_tuning = settings_->GetTextureTuning();
    auto model
      = content::import::SceneImportSettings::FromOptions(options, "normalize");
    model.source_path = source_path.string();
    const auto content_root = file_browser_->GetContentRoots().content_root;
    const auto record = content::import::RetainedModelImport::RecordPath(
      content_root, source_path);
    const auto recipe = content::import::RetainedModelImport::MakeRecipe(
      model, settings_->GetDefaultLayout());
    content::import::RetainedModelImport::SaveRecipe(
      record, content_root, recipe);
    auto publication = content::import::RetainedModelImport::Prepare(record);
    const auto job_id = import_service_->SubmitRetainedImport(
      std::move(publication),
      [this](content::import::ImportJobId id,
        const content::import::ImportReport& report) -> void {
        OnImportComplete(id, report);
      },
      [this](const content::import::ProgressEvent& progress) -> void {
        OnImportProgress(progress);
      });
    if (!job_id) {
      throw std::runtime_error("Import service could not accept this request");
    }
    import_state_.job_id = *job_id;
  } catch (const std::exception& error) {
    import_state_.is_importing.store(false);
    AddDiagnosticMarker(std::string("Import failed: ") + error.what(), false);
    LOG_F(ERROR, "Content import failed: {}", error.what());
  }
}

auto ContentVm::CancelActiveImport() -> void
{
  if (!IsImportInProgress()) {
    return;
  }
  import_state_.cancel_requested = true;
  import_service_->CancelJob(import_state_.job_id);
}

auto ContentVm::BrowseForModelRoot() -> void
{
  if (!file_browser_) {
    return;
  }
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
  if (!file_browser_) {
    return;
  }
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
  } else if (!std::filesystem::exists(base::ToNativePath(s.model_root), ec)) {
    LOG_F(WARNING, "ContentVm: Model root does not exist: '{}' (error: {})",
      s.model_root.string(), ec.message());
  } else if (!std::filesystem::is_directory(
               base::ToNativePath(s.model_root), ec)) {
    LOG_F(WARNING, "ContentVm: Model root is not a directory: '{}' (error: {})",
      s.model_root.string(), ec.message());
  } else {
    LOG_F(INFO, "ContentVm: Scanning model root for FBX/GLB/GLTF files...");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
           base::ToNativePath(s.model_root), ec)) {
      if (ec) {
        LOG_F(ERROR, "ContentVm: Error during iteration at '{}': {}",
          entry.path().string(), ec.message());
        ec.clear(); // Clear so we can potentially continue or at least not fail
                    // the whole function
        continue;
      }
      if (!entry.is_regular_file()) {
        continue;
      }

      const auto ext = entry.path().extension().string();
      if (s.include_fbx && ext == ".fbx") {
        sources.push_back({
          base::ToLogicalPath(entry.path()),
          content::import::ImportFormat::kFbx,
        });
        DLOG_F(INFO, "ContentVm: Found FBX: {}", entry.path().string());
      } else if (s.include_glb && ext == ".glb") {
        sources.push_back({
          base::ToLogicalPath(entry.path()),
          content::import::ImportFormat::kGltf,
        });
        DLOG_F(INFO, "ContentVm: Found GLB: {}", entry.path().string());
      } else if (s.include_gltf && ext == ".gltf") {
        sources.push_back({
          base::ToLogicalPath(entry.path()),
          content::import::ImportFormat::kGltf,
        });
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
  if (std::filesystem::is_directory(
        base::ToNativePath(roots.pak_directory), ec)) {
    for (const auto& entry : std::filesystem::directory_iterator(
           base::ToNativePath(roots.pak_directory), ec)) {
      if (entry.path().extension() == ".pak") {
        paks.push_back(base::ToLogicalPath(entry.path()));
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

auto ContentVm::SetOnGenerationPublished(
  std::function<void(const GenerationPublication&)> callback) -> void
{
  on_generation_published_ = std::move(callback);
}

auto ContentVm::SetOnClearMounts(std::function<void()> callback) -> void
{
  on_clear_mounts_ = std::move(callback);
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
    const bool is_dir
      = std::filesystem::is_directory(base::ToNativePath(path), ec);
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
    if (std::filesystem::exists(base::ToNativePath(index_path.parent_path()
          / data::loose_cooked::kGenerationLeaseFileName))) {
      if (const auto record = FindRetainedRecord(index_path)) {
        LoadImportRecord(*record);
      } else {
        AddDiagnosticMarker(
          "Select this library's authored .import.json record.", false);
      }
      return;
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

auto ContentVm::MountGeneration(const GenerationPublication& publication)
  -> void
{
  if (on_generation_published_) {
    on_generation_published_(publication);
  } else if (asset_loader_) {
    std::optional<data::SourceKey> previous;
    std::vector<data::SourceKey> older;
    bool selected_is_mounted = false;
    const auto selected = runtime::NormalizePath(publication.cooked_root);
    for (const auto& source : asset_loader_->EnumerateMountedSources()) {
      const auto mounted = runtime::NormalizePath(source.source_path);
      if (source.source_kind
          == content::IAssetLoader::ContentSourceKind::kLooseCooked
        && mounted == selected) {
        selected_is_mounted = true;
      } else if (source.source_kind
          == content::IAssetLoader::ContentSourceKind::kLooseCooked
        && mounted.parent_path() == selected.parent_path()) {
        previous = source.source_key;
        older.push_back(source.source_key);
      }
    }
    if (selected_is_mounted) {
      previous.reset();
    }
    static_cast<void>(asset_loader_->MountLooseCookedGeneration(
      publication.cooked_root, previous));
    for (const auto key : older) {
      if (!previous || key != *previous) {
        static_cast<void>(asset_loader_->RetireLooseCookedGeneration(key));
      }
    }
    RefreshLibrary();
    PersistMountedSources();
  }
}

auto ContentVm::LoadImportRecord(const std::filesystem::path& path) -> void
{
  try {
    const auto record = runtime::NormalizePath(path);
    const auto root
      = content::import::RetainedModelImport::SelectedGeneration(record);
    if (!root
      || !std::filesystem::exists(
        base::ToNativePath(*root / "container.index.bin"))) {
      AddDiagnosticMarker(
        "Retained import needs recooking: " + record.string(), false);
      return;
    }
    MountGeneration(
      GenerationPublication { .cooked_root = *root, .previous_root = {} });
    if (std::ranges::find(loaded_import_records_, record)
      == loaded_import_records_.end()) {
      loaded_import_records_.push_back(record);
    }
    settings_->SetMountedImportRecords(loaded_import_records_);
  } catch (const std::exception& error) {
    AddDiagnosticMarker(
      std::string("Retained import unavailable: ") + error.what(), false);
  }
}

auto ContentVm::UnloadAllLibrary() -> void
{
  {
    std::lock_guard lock(data_mutex_);
    loaded_paks_.clear();
    loaded_indices_.clear();
    loaded_import_records_.clear();
    scenes_map_.clear();
    available_scenes_.clear();
  }

  if (settings_) {
    settings_->SetMountedPakPaths({});
    settings_->SetMountedIndexPaths({});
    settings_->SetMountedImportRecords({});
    settings_->SetActiveSceneSelection(std::nullopt);
  }

  if (on_clear_mounts_) {
    on_clear_mounts_();
  } else if (asset_loader_) {
    asset_loader_->ClearMounts();
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

  // Mount callbacks may persist the current partial library immediately.
  const auto paks = settings_->GetMountedPakPaths();
  const auto indices = settings_->GetMountedIndexPaths();
  loaded_import_records_ = settings_->GetMountedImportRecords();
  const auto selection = settings_->GetActiveSceneSelection();
  for (const auto& pak_path : paks) {
    if (!pak_path.empty()) {
      MountPak(pak_path);
    }
  }
  for (const auto& index_path : indices) {
    if (!index_path.empty()) {
      LoadIndex(index_path);
    }
  }
  const auto records = loaded_import_records_;
  for (const auto& record : records) {
    LoadImportRecord(record);
  }

  RefreshLibrary();

  if (!selection.has_value()) {
    return;
  }
  pending_scene_selection_restore_ = selection;
  if (!pending_scene_selection_restore_->import_record_path.empty()) {
    try {
      if (const auto root
        = content::import::RetainedModelImport::SelectedGeneration(
          pending_scene_selection_restore_->import_record_path)) {
        pending_scene_selection_restore_->source_path
          = *root / "container.index.bin";
        pending_scene_selection_restore_->source_is_pak = false;
      }
    } catch (const std::exception& error) {
      AddDiagnosticMarker(
        std::string("Retained scene needs attention: ") + error.what(), false);
    }
  }
  TryResolvePendingSceneSelection();
}

auto ContentVm::PersistLibraryState() -> void { PersistMountedSources(); }

auto ContentVm::PrunePersistedMountedSource(
  const SceneSourceKind source_kind, const std::filesystem::path& path) -> void
{
  if (!settings_ || path.empty()) {
    return;
  }

  // A failed generation mount does not remove the authored library intent.
  // Retained records remain retryable until the user explicitly clears them.
  if (source_kind == SceneSourceKind::kLooseIndex
    && FindRetainedRecord(path).has_value()) {
    return;
  }

  const auto normalized_target = NormalizePathForKey(path);
  auto remove_matching_path
    = [&normalized_target](std::vector<std::filesystem::path>& paths) -> bool {
    const auto original_size = paths.size();
    std::erase_if(
      paths, [&normalized_target](const std::filesystem::path& entry) -> bool {
        return NormalizePathForKey(entry) == normalized_target;
      });
    return paths.size() != original_size;
  };

  auto removed = false;
  if (source_kind == SceneSourceKind::kPak) {
    auto pak_paths = settings_->GetMountedPakPaths();
    removed = remove_matching_path(pak_paths);
    if (removed) {
      settings_->SetMountedPakPaths(pak_paths);
    }
  } else {
    auto index_paths = settings_->GetMountedIndexPaths();
    removed = remove_matching_path(index_paths) || removed;
    if (removed) {
      settings_->SetMountedIndexPaths(index_paths);
    }
  }

  if (removed) {
    PruneActiveSceneSelectionForSource(source_kind, path);
  }
}

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
  if (!file_browser_) {
    return;
  }
  auto config = MakePakFileBrowserConfig(file_browser_->GetContentRoots());
  browse_request_id_ = file_browser_->Open(config);
  browse_mode_ = BrowseMode::kPakFile;
}

auto ContentVm::BrowseForLibrary() -> void
{
  if (!file_browser_) {
    return;
  }
  auto config = MakeLibraryBrowserConfig(file_browser_->GetContentRoots());
  browse_request_id_ = file_browser_->Open(config);
  browse_mode_ = BrowseMode::kLibraryFile;
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
  std::vector<std::filesystem::path> all_indices;
  if (asset_loader_) {
    for (const auto& mounted_source :
      asset_loader_->EnumerateMountedSources()) {
      if (mounted_source.source_kind
        == content::IAssetLoader::ContentSourceKind::kPak) {
        paks.push_back(mounted_source.source_path);
      } else {
        const auto index = mounted_source.source_path / "container.index.bin";
        all_indices.push_back(index);
        if (!std::filesystem::exists(
              base::ToNativePath(mounted_source.source_path
                / data::loose_cooked::kGenerationLeaseFileName))) {
          indices.push_back(index);
        }
      }
    }
    {
      std::lock_guard lock(data_mutex_);
      loaded_paks_ = paks;
      loaded_indices_ = all_indices;
    }
  } else {
    std::lock_guard lock(data_mutex_);
    paks = loaded_paks_;
    indices = loaded_indices_;
  }
  settings_->SetMountedPakPaths(paks);
  settings_->SetMountedIndexPaths(indices);
  settings_->SetMountedImportRecords(loaded_import_records_);
}

auto ContentVm::FindRetainedRecord(const std::filesystem::path& index_path)
  -> std::optional<std::filesystem::path>
{
  const auto generation_parent
    = runtime::NormalizePath(index_path).parent_path().parent_path();
  for (const auto& record : loaded_import_records_) {
    try {
      const auto selected
        = content::import::RetainedModelImport::SelectedGeneration(record);
      if (selected
        && runtime::NormalizePath(*selected).parent_path()
          == generation_parent) {
        return record;
      }
    } catch (const std::exception& error) {
      AddDiagnosticMarker(
        std::string("Retained import unavailable: ") + error.what(), false);
    }
  }
  return std::nullopt;
}

auto ContentVm::PersistActiveSceneSelection(const SceneEntry& entry) -> void
{
  if (!settings_) {
    return;
  }

  const auto import_record = entry.source.kind == SceneSourceKind::kLooseIndex
    ? FindRetainedRecord(entry.source.path)
    : std::nullopt;
  settings_->SetActiveSceneSelection(ContentActiveSceneSelection {
    .scene_name = entry.name,
    .scene_key = nostd::to_string(entry.key),
    .source_path = entry.source.path,
    .source_is_pak = entry.source.kind == SceneSourceKind::kPak,
    .import_record_path = import_record.value_or(std::filesystem::path {}),
  });
}

auto ContentVm::PruneActiveSceneSelectionForSource(
  const SceneSourceKind source_kind, const std::filesystem::path& path) -> void
{
  if (!settings_ || path.empty()) {
    return;
  }

  const auto selection = settings_->GetActiveSceneSelection();
  if (!selection.has_value()) {
    return;
  }

  const auto matches_kind
    = selection->source_is_pak == (source_kind == SceneSourceKind::kPak);
  if (!matches_kind) {
    return;
  }

  if (NormalizePathForKey(selection->source_path)
    != NormalizePathForKey(path)) {
    return;
  }

  settings_->SetActiveSceneSelection(std::nullopt);
  pending_scene_selection_restore_.reset();
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
      if (selection.scene_key.empty()
        && (selection.scene_name.empty()
          || scene.name != selection.scene_name)) {
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

auto ContentVm::GetGeneratedStorageRoot() const -> std::string
{
  return file_browser_
    ? (file_browser_->GetContentRoots().content_root / ".cooked" / "imports")
        .string()
    : std::string {};
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
  if (import_state_.job_id != job_id) {
    return;
  }

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

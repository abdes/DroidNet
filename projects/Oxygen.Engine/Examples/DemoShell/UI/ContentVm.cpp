//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
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
#include "DemoShell/UI/ContentVm.h"
#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Core/Types/AssetKey.h>

namespace oxygen::examples::ui {

namespace {
  using content::import::ModelImportRecord;

  //! `Showcase showcase.pak (#4)`: a row as log rows name it.
  auto RowName(const CookedSourceRow& row, const std::size_t index)
    -> std::string
  {
    return fmt::format(
      "{} {} (#{})", MountNameOf(row), row.path.filename().string(), index + 1);
  }

  auto Plural(const std::size_t count, const std::string_view word)
    -> std::string
  {
    return fmt::format("{} {}{}", count, word, count == 1 ? "" : "s");
  }
} // namespace

ContentVm::ContentVm(const observer_ptr<ContentSettingsService> settings,
  const observer_ptr<FileBrowserService> file_browser,
  const observer_ptr<content::IAssetLoader> asset_loader)
  : settings_(settings)
  , file_browser_(file_browser)
  , asset_loader_(asset_loader)
{
  DCHECK_NOTNULL_F(settings_, "ContentVm requires its settings");
  profile_ = settings_->GetImportProfile();
  import_tab_ = settings_->GetImportTab();
  running_importer_ = profile_.importer;
  import_service_
    = std::make_unique<content::import::AsyncImportService>(running_importer_);

  LoadScratchMounts();
}

ContentVm::~ContentVm()
{
  if (import_service_) {
    import_service_->Stop();
  }
}

// --- Host ---

auto ContentVm::SetOnMountsRequested(
  std::function<void(std::vector<content::ContentMount>)> callback) -> void
{
  on_mounts_requested_ = std::move(callback);
}

auto ContentVm::SetOnGenerationPublished(
  std::function<void(const runtime::GenerationPublication&)> callback) -> void
{
  on_generation_published_ = std::move(callback);
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

auto ContentVm::SetOnSceneUnloadRequested(std::function<void()> callback)
  -> void
{
  on_scene_unload_requested_ = std::move(callback);
}

auto ContentVm::RestorePersistedState() -> void
{
  if (restored_) {
    return;
  }
  restored_ = true;
  mounts_.SetAdded(settings_->GetMountSources());
  ResolveRecords(settings_->GetMountedImportRecords());
  scene_to_restore_ = settings_->GetLoadedScene();
  RequestMounts();
}

auto ContentVm::Update() -> void
{
  if (remount_pending_ && !mounting_) {
    remount_pending_ = false;
    RequestMounts();
  }
  if (model_states_dirty_) {
    model_states_dirty_ = false;
    RefreshModelStates();
  }

  auto report = std::optional<content::import::ImportReport> {};
  {
    std::scoped_lock lock(import_.mutex);
    report = std::exchange(import_.report, std::nullopt);
  }
  if (report) {
    FinishImport(*report);
  }
}

// --- Mounts ---

auto ContentVm::AddSource(const ContentSourceSetting& source) -> void
{
  if (!mounts_.Add(source)) {
    return;
  }
  PersistMounts();
  RequestMounts();
}

auto ContentVm::RemoveSource(const std::size_t row) -> void
{
  if (!mounts_.Remove(row)) {
    return;
  }
  PersistMounts();
  RequestMounts();
}

auto ContentVm::MoveSource(const ContentMountList::RowMove move) -> void
{
  if (!mounts_.Move(move) || move.from == move.to) {
    return;
  }
  PersistMounts();
  RequestMounts();
}

auto ContentVm::Remount() -> void
{
  ResolveRecords(mounts_.Records());
  RequestMounts();
}

auto ContentVm::SourceDetailsOf(const std::size_t row, std::string& error)
  -> const SourceDetails*
{
  if (row >= mounts_.Rows().size()) {
    error = "No such source";
    return nullptr;
  }
  const auto& path = mounts_.Rows().at(row).path;
  if (const auto found = details_.find(path); found != details_.end()) {
    return &found->second;
  }
  if (const auto failed = details_errors_.find(path);
    failed != details_errors_.end()) {
    error = failed->second;
    return nullptr;
  }
  try {
    return &details_.emplace(path, ReadSourceDetails(path)).first->second;
  } catch (const std::exception& read_error) {
    error = details_errors_.emplace(path, read_error.what()).first->second;
    return nullptr;
  }
}

auto ContentVm::LoadScratchMounts() -> void
{
  if (file_browser_ && !file_browser_->GetContentRoots().scratch_root.empty()) {
    try {
      models_.SetMounts(MakeScratchMounts(file_browser_->GetContentRoots()));
    } catch (const std::exception& error) {
      log_.Add({
        .severity = LogSeverity::kError,
        .code = std::string(log_codes::kImportRecordUnavailable),
        .message = error.what(),
      });
    }
  }
  RefreshModels();
}

auto ContentVm::LocalMounts() const -> std::filesystem::path
{
  if (!file_browser_ || file_browser_->GetContentRoots().scratch_root.empty()) {
    return {};
  }
  return LocalMountsFile(file_browser_->GetContentRoots().scratch_root);
}

auto ContentVm::SourceFolders() const -> std::vector<SourceFolder>
{
  const auto local = LocalMounts();
  auto folders = std::vector<SourceFolder> {};
  for (const auto& mount : models_.SourceMounts()) {
    std::error_code error;
    folders.push_back({
      .name = mount.name,
      .folder = mount.folder,
      .removable = !mount.mounts_file.empty()
        && std::filesystem::equivalent(mount.mounts_file, local, error),
    });
  }
  return folders;
}

auto ContentVm::AddSourceFolder(const std::filesystem::path& folder)
  -> std::optional<std::string>
{
  const auto* mounts = models_.Mounts();
  const auto file = LocalMounts();
  try {
    if (mounts == nullptr || file.empty()) {
      throw std::runtime_error("Imports are unavailable: no scratch project");
    }
    const auto canonical = std::filesystem::canonical(folder);
    if (!std::filesystem::is_directory(canonical)) {
      throw std::runtime_error(canonical.string() + " is not a folder");
    }
    auto taken = std::vector<std::string> {};
    for (const auto& mount : mounts->mounts) {
      taken.push_back(mount.name);
    }
    auto name = SourceFolderMountName(canonical, taken);
    AddLocalMount(file, name, canonical);
    LoadScratchMounts();
    log_.Add({
      .severity = LogSeverity::kInfo,
      .code = std::string(log_codes::kSourceFolderAdded),
      .message = fmt::format("Mounted {} as /{}", canonical.string(), name),
      .path = canonical.generic_string(),
    });
    return name;
  } catch (const std::exception& error) {
    log_.Add({
      .severity = LogSeverity::kError,
      .code = std::string(log_codes::kSourceFolderFailed),
      .message = "The source folder was not added",
      .details = error.what(),
      .path = folder.generic_string(),
    });
    return std::nullopt;
  }
}

auto ContentVm::RemoveSourceFolder(const std::string_view name) -> void
{
  const auto folders = SourceFolders();
  const auto found = std::ranges::find(folders, name, &SourceFolder::name);
  if (found == folders.end() || !found->removable) {
    return;
  }
  try {
    RemoveLocalMount(LocalMounts(), name);
  } catch (const std::exception& error) {
    log_.Add({
      .severity = LogSeverity::kError,
      .code = std::string(log_codes::kSourceFolderFailed),
      .message = fmt::format("/{} was not removed", name),
      .details = error.what(),
      .path = LocalMounts().generic_string(),
    });
    return;
  }
  LoadScratchMounts();
}

auto ContentVm::BrowseForPak() -> void
{
  if (file_browser_) {
    OpenBrowser(MakePakFileBrowserConfig(file_browser_->GetContentRoots()),
      BrowseTarget::kPakFile);
  }
}

auto ContentVm::BrowseForCookedRoot() -> void
{
  if (file_browser_) {
    OpenBrowser(MakeCookedRootBrowserConfig(file_browser_->GetContentRoots()),
      BrowseTarget::kCookedRoot);
  }
}

auto ContentVm::BrowseForSourceFolder() -> void
{
  if (file_browser_) {
    OpenBrowser(MakeSourceFolderBrowserConfig(file_browser_->GetContentRoots()),
      BrowseTarget::kSourceFolder);
  }
}

auto ContentVm::BrowseForModelFile() -> void
{
  if (file_browser_) {
    OpenBrowser(MakeModelFileBrowserConfig(file_browser_->GetContentRoots(),
                  {
                    std::string(kFbxExtension),
                    std::string(kGlbExtension),
                    std::string(kGltfExtension),
                  }),
      BrowseTarget::kModelFile);
  }
}

auto ContentVm::ConsumeBrowseSelection() -> std::optional<BrowseSelection>
{
  if (!file_browser_ || browse_request_id_ == 0 || !browse_target_) {
    return std::nullopt;
  }
  const auto result = file_browser_->ConsumeResult(browse_request_id_);
  if (!result) {
    return std::nullopt;
  }
  const auto target = *browse_target_;
  browse_target_.reset();
  browse_request_id_ = 0;
  if (result->kind != FileBrowserService::ResultKind::kSelected) {
    return std::nullopt;
  }
  return BrowseSelection { .target = target, .path = result->path };
}

auto ContentVm::OnMountsApplied(const runtime::MountOutcome& outcome) -> void
{
  mounting_ = false;
  if (outcome.retry) {
    remount_pending_ = true;
    return;
  }
  mounts_.ApplyOutcome(outcome,
    asset_loader_ ? asset_loader_->EnumerateMountedSources()
                  : std::vector<content::IAssetLoader::MountedSourceEntry> {});
  details_.clear();
  details_errors_.clear();

  const auto& rows = mounts_.Rows();
  if (outcome.error) {
    log_.Add({
      .severity = LogSeverity::kError,
      .code = std::string(log_codes::kMountFailed),
      .message = "The mount list was not applied; the mounts are unchanged",
      .details = *outcome.error,
    });
  }
  for (const auto& refusal : outcome.refused) {
    const auto index = mounts_.IndexOf(refusal.pak);
    auto searched = std::vector<std::string> {};
    for (auto below = index.value_or(rows.size()) + 1; below < rows.size();
      ++below) {
      searched.push_back(RowName(rows.at(below), below));
    }
    log_.Add({
      .severity = LogSeverity::kError,
      .code = std::string(log_codes::kMountRefused),
      .message = fmt::format("{} refused: {} not met",
        index ? RowName(rows.at(*index), *index)
              : refusal.pak.filename().string(),
        Plural(refusal.failures.size(), "requirement")),
      .details = refusal.message,
      .path = refusal.pak.generic_string(),
      .searched = std::move(searched),
      .cooked_source = refusal.pak,
    });
  }
  if (!outcome.error) {
    log_.Add({
      .severity = LogSeverity::kInfo,
      .code = std::string(log_codes::kMountsApplied),
      .message = Plural(outcome.mounted.size(), "cooked source") + " mounted",
    });
  }

  RefreshScenes();
  TryRestoreScene();
  if (remount_pending_) {
    remount_pending_ = false;
    RequestMounts();
  }
}

auto ContentVm::OnGenerationMounted(
  const runtime::GenerationPublication& publication,
  const std::optional<std::string>& error) -> void
{
  const auto row = mounts_.IndexOf(publication.cooked_root);
  if (row) {
    mounts_.InspectRow(*row);
  }
  if (error) {
    if (row) {
      mounts_.SetRowState(*row, SourceState::kUnreadable, *error);
    }
    log_.Add({
      .severity = LogSeverity::kError,
      .code = std::string(log_codes::kGenerationMountFailed),
      .message = "The model's new generation was not mounted",
      .details = *error,
      .path = publication.cooked_root.generic_string(),
      .cooked_source = publication.cooked_root,
    });
  } else if (row) {
    mounts_.SetRowState(*row, SourceState::kMounted, {});
  }
  RefreshScenes();
  if (error) {
    imported_products_.clear();
  } else {
    LoadImportedScene();
  }
}

auto ContentVm::RequestMounts() -> void
{
  if (mounting_) {
    remount_pending_ = true;
    return;
  }
  auto mounts = mounts_.Inspect();
  const auto& rows = mounts_.Rows();
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto& row = rows.at(index);
    if (row.state != SourceState::kMissing
      && row.state != SourceState::kUnreadable) {
      continue;
    }
    const auto missing = row.state == SourceState::kMissing;
    log_.Add({
      .severity = missing ? LogSeverity::kWarning : LogSeverity::kError,
      .code = std::string(missing ? log_codes::kMountSourceMissing
                                  : log_codes::kMountSourceUnreadable),
      .message = fmt::format(
        "{} is {}", RowName(row, index), missing ? "missing" : "unreadable"),
      .details = row.error,
      .path = row.path.generic_string(),
      .cooked_source = row.path,
    });
  }
  if (!on_mounts_requested_) {
    // Without a host nothing mounts; the rows stay pending.
    return;
  }
  mounting_ = true;
  on_mounts_requested_(std::move(mounts));
}

auto ContentVm::ResolveRecords(
  const std::vector<std::filesystem::path>& records) -> void
{
  auto generations = std::vector<RecordGeneration> {};
  for (const auto& record : records) {
    auto entry = RecordGeneration { .record = record };
    try {
      if (const auto generation
        = ModelImportRecord::SelectedGeneration(record, Generations())) {
        entry.generation = *generation;
      } else {
        entry.error = "The record selects no generation; import it again";
      }
    } catch (const std::exception& error) {
      entry.error = error.what();
    }
    generations.push_back(std::move(entry));
  }
  mounts_.SetRecords(generations);
}

auto ContentVm::PersistMounts() const -> void
{
  settings_->SetMountSources(mounts_.Added());
  settings_->SetMountedImportRecords(mounts_.Records());
}

// --- Scenes ---

auto ContentVm::RefreshScenes() -> void
{
  if (!asset_loader_) {
    return;
  }
  scenes_.Rebuild(asset_loader_->EnumerateMountedScenes(),
    [this](const std::filesystem::path& source) -> std::optional<std::size_t> {
      return mounts_.IndexOf(source);
    });
}

auto ContentVm::TryRestoreScene() -> void
{
  if (!scene_to_restore_ || scene_load_) {
    return;
  }
  for (const auto& group : scenes_.Groups()) {
    for (const auto& scene : group.scenes) {
      if (nostd::to_string(scene.key) == scene_to_restore_->scene_key) {
        scene_to_restore_.reset();
        RequestSceneLoad(scene);
        return;
      }
    }
  }
}

auto ContentVm::RecentScenes() const -> std::vector<RecentSceneSetting>
{
  return settings_->GetRecentScenes();
}

auto ContentVm::RequestSceneLoad(const SceneEntry& scene) -> void
{
  if (scene_load_ || !on_scene_load_requested_) {
    return;
  }
  scene_load_
    = SceneLoad { .scene = scene, .started = std::chrono::steady_clock::now() };
  on_scene_load_requested_(scene);
}

auto ContentVm::ReloadScene() -> void
{
  if (loaded_scene_) {
    RequestSceneLoad(loaded_scene_->scene);
  }
}

auto ContentVm::UnloadScene() -> void
{
  if (!loaded_scene_ || scene_load_) {
    return;
  }
  if (on_scene_unload_requested_) {
    on_scene_unload_requested_();
  }
  loaded_scene_.reset();
  settings_->SetLoadedScene(std::nullopt);
}

auto ContentVm::CancelSceneLoad() -> void
{
  if (!scene_load_) {
    return;
  }
  if (on_scene_load_cancel_requested_) {
    on_scene_load_cancel_requested_();
  }
  log_.Add({
    .severity = LogSeverity::kInfo,
    .code = std::string(log_codes::kSceneLoadCancelled),
    .message = "Cancelled loading " + scene_load_->scene.name,
    .scene = scene_load_->scene.virtual_path,
  });
  scene_load_.reset();
}

auto ContentVm::LoadingScene() const noexcept -> const SceneEntry*
{
  return scene_load_ ? &scene_load_->scene : nullptr;
}

auto ContentVm::NotifySceneLoadCompleted(const data::AssetKey& key,
  const bool success, const std::string_view error) -> void
{
  if (!scene_load_ || scene_load_->scene.key != key) {
    return;
  }
  const auto load = std::move(*scene_load_);
  scene_load_.reset();
  const auto& scene = load.scene;
  if (!success) {
    log_.Add({
      .severity = LogSeverity::kError,
      .code = std::string(log_codes::kSceneLoadFailed),
      .message = scene.name + " did not load",
      .details = std::string(error),
      .scene = scene.virtual_path,
      .path = scene.virtual_path,
      .cooked_source = scene.source_path,
    });
    return;
  }
  auto sources = std::vector<std::filesystem::path> {};
  for (const auto& row : mounts_.Rows()) {
    if (row.state == SourceState::kMounted) {
      sources.push_back(row.path);
    }
  }
  const auto loaded = LoadedScene {
    .scene = scene,
    .asset_count = CountSceneAssets(key, sources),
    .load_time = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - load.started),
  };
  log_.Add({
    .severity = LogSeverity::kInfo,
    .code = std::string(log_codes::kSceneLoaded),
    .message = fmt::format("{}, {} in {} ms", scene.name,
      Plural(loaded.asset_count, "asset"), loaded.load_time.count()),
    .scene = scene.virtual_path,
    .cooked_source = scene.source_path,
  });
  RememberLoadedScene(loaded);
}

auto ContentVm::RememberLoadedScene(const LoadedScene& loaded) -> void
{
  loaded_scene_ = loaded;
  const auto key = std::string(nostd::to_string(loaded.scene.key));
  settings_->SetLoadedScene(ContentSceneSetting {
    .scene_key = key,
    .virtual_path = loaded.scene.virtual_path,
    .source = {
      .kind = loaded.scene.source_kind,
      .path = loaded.scene.source_path,
    },
  });
  auto recent = settings_->GetRecentScenes();
  std::erase_if(recent, [&key](const RecentSceneSetting& scene) -> bool {
    return scene.scene_key == key;
  });
  recent.insert(recent.begin(),
    RecentSceneSetting {
      .scene_key = key,
      .virtual_path = loaded.scene.virtual_path,
    });
  if (recent.size() > ContentSettingsService::kRecentSceneCount) {
    recent.resize(ContentSettingsService::kRecentSceneCount);
  }
  settings_->SetRecentScenes(recent);
}

// --- Import ---

auto ContentVm::SetImportTab(const ContentImportTabSettings& tab) -> void
{
  if (tab == import_tab_) {
    return;
  }
  const auto rescan = tab.source_mount != import_tab_.source_mount
    || tab.fbx != import_tab_.fbx || tab.glb != import_tab_.glb
    || tab.gltf != import_tab_.gltf;
  import_tab_ = tab;
  settings_->SetImportTab(import_tab_);
  if (rescan) {
    RefreshModels();
  }
}

auto ContentVm::RefreshModels() -> void
{
  auto mount = import_tab_.source_mount;
  const auto source_mounts = models_.SourceMounts();
  const auto known
    = std::ranges::contains(source_mounts, mount, &SourceMountEntry::name);
  if (!known && !source_mounts.empty()) {
    mount = source_mounts.front().name;
  }
  models_.Scan(mount,
    {
      .fbx = import_tab_.fbx,
      .glb = import_tab_.glb,
      .gltf = import_tab_.gltf,
    });
  record_view_.reset();
  RefreshModelStates();
}

auto ContentVm::RefreshModelStates() -> void
{
  models_.Refresh(PanelSettingsJson(), Generations());
}

auto ContentVm::ModelRecordOf(const std::filesystem::path& file)
  -> const ModelRecordView&
{
  if (!record_view_ || record_view_model_ != file) {
    const auto* model = models_.Find(file);
    record_view_ = model != nullptr
      ? models_.ReadRecord(*model, PanelSettingsJson(), Generations())
      : ModelRecordView { .error = "No such model" };
    record_view_model_ = file;
  }
  return *record_view_;
}

auto ContentVm::SetImportProfile(const ImportProfile& profile) -> void
{
  profile_ = profile;
  settings_->SetImportProfile(profile_);
  record_view_.reset();
  model_states_dirty_ = true;
}

auto ContentVm::ChangedImportGroups() const -> std::vector<ImportSettingsGroup>
{
  return ChangedGroups(profile_);
}

auto ContentVm::ResetImportGroup(const ImportSettingsGroup group) -> void
{
  SetImportProfile(ResetGroup(profile_, group));
}

auto ContentVm::IsImporterRestartPending() const -> bool
{
  return !SameImporterConfig(profile_.importer, running_importer_);
}

auto ContentVm::RestartImportService() -> void
{
  if (IsImportInProgress()) {
    return;
  }
  if (import_service_) {
    import_service_->Stop();
  }
  running_importer_ = profile_.importer;
  import_service_
    = std::make_unique<content::import::AsyncImportService>(running_importer_);
}

auto ContentVm::StartImport(const std::filesystem::path& model_file,
  const ImportSettingsSource settings) -> void
{
  if (IsImportInProgress()) {
    return;
  }
  const auto* model = models_.Find(model_file);
  const auto* mounts = models_.Mounts();
  try {
    if (model == nullptr || mounts == nullptr) {
      throw std::invalid_argument(
        "Not a model of a source mount: " + model_file.string());
    }
    auto location = ModelImportRecord::LocateRecord({
      .source = model->file,
      .identity = model->identity,
      .mounts = make_observer(mounts),
    });
    if (settings == ImportSettingsSource::kPanel) {
      auto model_settings = MakeModelImportSettings(profile_);
      model_settings.source_path = model->file.string();
      model_settings.virtual_path = model->identity;
      ModelImportRecord::Save({
        .record_path = location.record_path,
        .source = model->file,
        .settings_json = ModelImportRecord::MakeSettings(model_settings),
        .mounts = make_observer(mounts),
      });
    } else if (!location.exists) {
      throw std::invalid_argument(
        model->file.filename().string() + " has no record to import as is");
    }
    auto publication = ModelImportRecord::Prepare(location.record_path,
      Generations(), *mounts, std::move(location.diagnostics));
    import_.model = model->file;
    import_.running = true;
    import_cancel_requested_ = false;
    {
      std::scoped_lock lock(import_.mutex);
      import_.progress = {};
    }
    const auto job_id = import_service_->SubmitModelImport(
      std::move(publication),
      [this](const content::import::ImportJobId id,
        const content::import::ImportReport& report) -> void {
        std::scoped_lock lock(import_.mutex);
        if (id == import_.job_id
          || import_.job_id == content::import::kInvalidJobId) {
          import_.report = report;
        }
      },
      [this](const content::import::ProgressEvent& progress) -> void {
        std::scoped_lock lock(import_.mutex);
        import_.progress = progress;
      });
    if (!job_id) {
      throw std::runtime_error("The importer did not accept the import");
    }
    {
      std::scoped_lock lock(import_.mutex);
      import_.job_id = *job_id;
    }
    models_.SetImporting(model->file);
  } catch (const std::exception& error) {
    import_.running = false;
    models_.SetImportFinished(model_file, false);
    model_states_dirty_ = true;
    log_.Add({
      .severity = LogSeverity::kError,
      .code = std::string(log_codes::kImportFailed),
      .message = model_file.filename().string() + " was not imported",
      .details = error.what(),
      .path = model_file.generic_string(),
    });
  }
}

auto ContentVm::ImportFile(const std::filesystem::path& file) -> void
{
  if (IsImportInProgress()) {
    return;
  }
  std::error_code error;
  const auto model = std::filesystem::weakly_canonical(file, error);
  auto mount = std::optional<std::string> {};
  for (const auto& folder : models_.SourceMounts()) {
    const auto relative = model.lexically_relative(folder.folder);
    if (!relative.empty() && *relative.begin() != "..") {
      mount = folder.name;
    }
  }
  if (!mount) {
    mount = AddSourceFolder(model.parent_path());
    if (!mount) {
      return;
    }
  }
  // The model is listed: its folder's mount, its format shown.
  auto tab = import_tab_;
  tab.source_mount = *mount;
  const auto own = ContentModelCatalog::FormatsOf(model);
  tab.fbx = tab.fbx || own.fbx;
  tab.glb = tab.glb || own.glb;
  tab.gltf = tab.gltf || own.gltf;
  SetImportTab(tab);
  StartImport(model);
}

auto ContentVm::CancelActiveImport() -> void
{
  if (!IsImportInProgress()) {
    return;
  }
  import_cancel_requested_ = true;
  auto job_id = content::import::kInvalidJobId;
  {
    std::scoped_lock lock(import_.mutex);
    job_id = import_.job_id;
  }
  // False means the job already finished; its completion ends the import.
  static_cast<void>(import_service_->CancelJob(job_id));
}

auto ContentVm::IsImportInProgress() const -> bool
{
  return import_.running.load();
}

auto ContentVm::GetImportProgress() const -> ImportProgressView
{
  std::scoped_lock lock(import_.mutex);
  auto view = ImportProgressView {
    .model = import_.model,
    .phase = content::import::to_string(import_.progress.header.phase),
    .progress = import_.progress.header.overall_progress,
  };
  if (const auto* item
    = std::get_if<content::import::ItemProgress>(&import_.progress.payload)) {
    view.item = item->item_kind + " " + item->item_name;
  }
  return view;
}

auto ContentVm::FinishImport(const content::import::ImportReport& report)
  -> void
{
  const auto model = import_.model;
  const auto name = model.filename().string();
  {
    std::scoped_lock lock(import_.mutex);
    import_.job_id = content::import::kInvalidJobId;
  }
  import_.running = false;
  for (const auto& diagnostic : report.diagnostics) {
    log_.AddDiagnostic(diagnostic);
  }
  models_.SetImportFinished(model, report.success || import_cancel_requested_);
  record_view_.reset();
  model_states_dirty_ = true;
  if (!report.success) {
    const auto errors = std::ranges::count(report.diagnostics,
      LogSeverity::kError, &content::import::ImportDiagnostic::severity);
    log_.Add({
      .severity
      = import_cancel_requested_ ? LogSeverity::kInfo : LogSeverity::kError,
      .code = std::string(import_cancel_requested_ ? log_codes::kImportCancelled
                                                   : log_codes::kImportFailed),
      .message = import_cancel_requested_
        ? "Cancelled the import of " + name
        : fmt::format("{} failed: {}", name,
            Plural(static_cast<std::size_t>(errors), "error")),
      .path = model.generic_string(),
    });
    return;
  }
  log_.Add({
    .severity = LogSeverity::kInfo,
    .code = std::string(log_codes::kImportPublished),
    .message = fmt::format(
      "{} published, {}", name, Plural(report.products.size(), "product")),
    .path = model.generic_string(),
    .cooked_source = report.cooked_root,
  });
  imported_products_.clear();
  if (import_tab_.load_scene_after_import) {
    for (const auto& product : report.products) {
      imported_products_.push_back(product.virtual_path);
    }
  }
  // A published generation loads its scene once it is mounted, which the
  // host may do on a later frame.
  if (!PublishGeneration(report)) {
    LoadImportedScene();
  }
}

auto ContentVm::PublishGeneration(const content::import::ImportReport& report)
  -> bool
{
  if (!report.model_record_path) {
    return false;
  }
  auto records = mounts_.Records();
  std::erase(records, *report.model_record_path);
  records.insert(records.begin(), *report.model_record_path);
  ResolveRecords(records);
  PersistMounts();
  const auto publication = runtime::GenerationPublication {
    .cooked_root = report.cooked_root,
    .previous_root = report.previous_cooked_root,
  };
  if (on_generation_published_) {
    on_generation_published_(publication);
    return true;
  }
  if (!asset_loader_) {
    return false;
  }
  try {
    runtime::MountGeneration(*asset_loader_, publication);
    OnGenerationMounted(publication, std::nullopt);
  } catch (const std::exception& error) {
    OnGenerationMounted(publication, std::string(error.what()));
  }
  return true;
}

auto ContentVm::LoadImportedScene() -> void
{
  const auto products = std::exchange(imported_products_, {});
  for (const auto& product : products) {
    for (const auto& group : scenes_.Groups()) {
      const auto found
        = std::ranges::find(group.scenes, product, &SceneEntry::virtual_path);
      if (found != group.scenes.end()) {
        RequestSceneLoad(*found);
        return;
      }
    }
  }
}

auto ContentVm::PanelSettingsJson() const -> std::string
{
  return ModelImportRecord::MakeSettings(MakeModelImportSettings(profile_));
}

auto ContentVm::Generations() const -> content::import::ModelImportGenerations
{
  return { .root = GetGeneratedStorageRoot() };
}

auto ContentVm::GetGeneratedStorageRoot() const -> std::filesystem::path
{
  if (!file_browser_ || file_browser_->GetContentRoots().scratch_root.empty()) {
    return {};
  }
  return file_browser_->GetContentRoots().scratch_root / ".cooked"
    / ModelImportRecord::kModelGenerationsFolder;
}

// --- Log ---

auto ContentVm::ClearLog() -> void { log_.Clear(); }

auto ContentVm::MarkLogSeen() -> void { log_.MarkSeen(); }

auto ContentVm::GetFileBrowser() const -> observer_ptr<FileBrowserService>
{
  return file_browser_;
}

auto ContentVm::OpenBrowser(
  const FileBrowserConfig& config, const BrowseTarget target) -> void
{
  browse_request_id_ = file_browser_->Open(config);
  browse_target_ = target;
}

} // namespace oxygen::examples::ui

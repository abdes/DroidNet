//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include "DemoShell/UI/ContentModelCatalog.h"
#include "DemoShell/UI/ContentTextMatch.h"

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::examples::ui {

namespace {
  //! The writable mount of the scratch project, which holds its records.
  constexpr auto kScratchMount = std::string_view { "Scratch" };

  auto LowerExtension(const std::filesystem::path& file) -> std::string
  {
    auto extension = file.extension().string();
    std::ranges::transform(
      extension, extension.begin(), [](const unsigned char ch) -> char {
        return static_cast<char>(std::tolower(ch));
      });
    return extension;
  }

  //! The format of a model file the formats admit, or none.
  auto FormatOf(const std::filesystem::path& file,
    const ContentModelCatalog::Formats formats)
    -> std::optional<content::import::ImportFormat>
  {
    const auto own = ContentModelCatalog::FormatsOf(file);
    if (own.fbx && formats.fbx) {
      return content::import::ImportFormat::kFbx;
    }
    if ((own.glb && formats.glb) || (own.gltf && formats.gltf)) {
      return content::import::ImportFormat::kGltf;
    }
    return std::nullopt;
  }

  //! The folders between the mount and the file of `/<Mount>/a/b/file`:
  //! `a/b`; empty for a file directly in its mount.
  auto FolderInMount(const std::string_view mount_path) -> std::string
  {
    auto folder = std::string {};
    const auto parent
      = std::filesystem::path(mount_path).relative_path().parent_path();
    if (parent.empty()) {
      return folder;
    }
    for (auto part = std::next(parent.begin()); part != parent.end(); ++part) {
      if (!folder.empty()) {
        folder += '/';
      }
      folder += part->generic_string();
    }
    return folder;
  }

  auto Exists(const std::filesystem::path& path) -> bool
  {
    std::error_code error;
    return !path.empty()
      && std::filesystem::exists(base::ToNativePath(path), error) && !error;
  }
} // namespace

auto ScratchModelVirtualPath(const std::string_view source_mount_path)
  -> std::string
{
  constexpr auto kSourceMedia = std::string_view { "/Content/SourceMedia/" };
  const auto relative = source_mount_path.starts_with(kSourceMedia)
    ? source_mount_path.substr(kSourceMedia.size())
    : source_mount_path.substr(1);
  return "/" + std::string(kScratchMount) + "/" + std::string(relative);
}

auto MakeScratchMounts(const ContentRootPaths& roots)
  -> content::import::MountTable
{
  using content::import::MountDeclaration;
  const auto& scratch_root = roots.scratch_root;
  const auto& examples_root = roots.content_root;
  std::filesystem::create_directories(scratch_root / kScratchMount);
  auto mounts = content::import::ResolveMountTable({
    .project_root = scratch_root,
    .declarations = {
      MountDeclaration {
        .name = std::string { kScratchMount },
        .kind = content::import::MountKind::kSource,
        .path = std::string { kScratchMount },
      },
      MountDeclaration {
        .name = "Content",
        .kind = content::import::MountKind::kSource,
        .path = (examples_root / "Content")
                  .lexically_relative(scratch_root)
                  .generic_string(),
      },
    },
    .form = content::import::MountDeclarationForm::kCommittedFile,
    .declared_in = "DemoShell scratch project",
    .working_directory = scratch_root,
    .mounts_option = std::nullopt,
    .owner = content::import::MountTableOwner::kProject,
  });
  if (!mounts.IsValid()) {
    throw std::runtime_error("The scratch project's mounts are invalid: "
      + mounts.diagnostics.front().message);
  }
  return mounts;
}

auto to_string(const ModelState state) noexcept -> std::string_view
{
  switch (state) {
  case ModelState::kNotImported:
    return "not imported";
  case ModelState::kImported:
    return "imported";
  case ModelState::kSettingsChanged:
    return "settings changed";
  case ModelState::kFailed:
    return "failed";
  case ModelState::kImporting:
    return "importing";
  }
  return "unknown";
}

auto ContentModelCatalog::SetMounts(content::import::MountTable mounts) -> void
{
  mounts_ = std::move(mounts);
  models_.clear();
}

auto ContentModelCatalog::Mounts() const -> const content::import::MountTable*
{
  return mounts_ ? &*mounts_ : nullptr;
}

auto ContentModelCatalog::SourceMounts() const -> std::vector<SourceMountEntry>
{
  auto entries = std::vector<SourceMountEntry> {};
  if (!mounts_) {
    return entries;
  }
  for (const auto& mount : mounts_->mounts) {
    // The scratch mount holds the imports' records, not models to pick.
    if (mount.kind == content::import::MountKind::kSource
      && !mount.folder.empty() && mount.name != kScratchMount) {
      entries.push_back({
        .name = mount.name,
        .folder = mount.folder,
        .mounts_file = mount.scope == content::import::MountScope::kLocal
          ? mount.origin_file
          : std::filesystem::path {},
      });
    }
  }
  std::ranges::sort(entries, {}, &SourceMountEntry::name);
  return entries;
}

auto ContentModelCatalog::Scan(
  const std::string_view name, const Formats formats) -> void
{
  models_.clear();
  if (!mounts_) {
    return;
  }
  const auto mount = mounts_->Find(name);
  if (!mount || mount->folder.empty()) {
    return;
  }
  std::error_code error;
  auto files = std::filesystem::recursive_directory_iterator(
    base::ToNativePath(mount->folder),
    std::filesystem::directory_options::skip_permission_denied, error);
  for (; !error && files != std::filesystem::recursive_directory_iterator {};
    files.increment(error)) {
    if (!files->is_regular_file(error)) {
      continue;
    }
    const auto file = base::ToLogicalPath(files->path());
    const auto format = FormatOf(file, formats);
    if (!format || mounts_->IsInOutputFolder(file)) {
      continue;
    }
    auto findings = std::vector<content::import::ImportDiagnostic> {};
    const auto located = mounts_->MountPathOf(file, findings);
    if (!located) {
      continue;
    }
    auto model = ModelEntry {
      .file = located->file,
      .mount_path = located->mount_path,
      .folder = FolderInMount(located->mount_path),
      .format = *format,
      .identity = ScratchModelVirtualPath(located->mount_path),
    };
    if (const auto record = content::import::ModelImportRecord::RecordPathOf(
          *mounts_, model.identity, findings)) {
      model.record = *record;
    }
    models_.push_back(std::move(model));
  }
  if (error) {
    LOG_F(WARNING, "Content models: scanning '{}' stopped: {}",
      mount->folder.string(), error.message());
  }
  // By band, then by file, so each folder's rows are together.
  std::ranges::sort(
    models_, [](const ModelEntry& lhs, const ModelEntry& rhs) -> bool {
      return std::tie(lhs.folder, lhs.mount_path)
        < std::tie(rhs.folder, rhs.mount_path);
    });
}

auto ContentModelCatalog::Refresh(const std::string_view panel_settings,
  const content::import::ModelImportGenerations& generations) -> void
{
  for (auto& model : models_) {
    if (model.file == importing_) {
      model.state = ModelState::kImporting;
      continue;
    }
    if (failed_.contains(model.file)) {
      model.state = ModelState::kFailed;
      continue;
    }
    const auto view = ReadRecord(model, panel_settings, generations);
    if (!view.error.empty() || view.generation.empty()) {
      model.state = ModelState::kNotImported;
    } else {
      model.state = view.differing.empty() ? ModelState::kImported
                                           : ModelState::kSettingsChanged;
    }
  }
}

auto ContentModelCatalog::Find(const std::filesystem::path& file) const
  -> const ModelEntry*
{
  const auto found = std::ranges::find(models_, file, &ModelEntry::file);
  return found == models_.end() ? nullptr : &*found;
}

auto ContentModelCatalog::ReadRecord(const ModelEntry& model,
  const std::string_view panel_settings,
  const content::import::ModelImportGenerations& generations) const
  -> ModelRecordView
{
  using content::import::ModelImportRecord;
  auto view = ModelRecordView { .record = model.record };
  if (!mounts_ || !Exists(model.record)) {
    view.error = "No record";
    return view;
  }
  try {
    const auto record = ModelImportRecord::Read(model.record, *mounts_);
    view.source = record.source_mount_path;
    view.settings_json = record.settings_json;
    view.differing = DifferingGroups(record.settings_json, panel_settings);
    if (!generations.root.empty()) {
      if (const auto generation
        = ModelImportRecord::SelectedGeneration(model.record, generations)) {
        view.generation = *generation;
      }
    }
  } catch (const std::exception& error) {
    view.error = error.what();
    return view;
  }
  if (view.generation.empty()) {
    return view;
  }
  std::error_code time_error;
  const auto published = std::filesystem::last_write_time(
    base::ToNativePath(view.generation), time_error);
  if (!time_error) {
    view.published = published;
  }
  try {
    const auto cooked = content::cooked::CookedContent::Open(
      content::cooked::RealCookerStorage(), view.generation);
    const auto scene = std::ranges::find(cooked.Assets(),
      data::AssetType::kScene, &content::cooked::CookedAsset::type);
    if (scene != cooked.Assets().end()) {
      view.scene = scene->virtual_path;
    }
  } catch (const std::exception& error) {
    view.error = error.what();
  }
  return view;
}

auto ContentModelCatalog::SetImporting(const std::filesystem::path& file)
  -> void
{
  importing_ = file;
  failed_.erase(file);
  for (auto& model : models_) {
    if (model.file == file) {
      model.state = ModelState::kImporting;
    }
  }
}

auto ContentModelCatalog::SetImportFinished(
  const std::filesystem::path& file, const bool success) -> void
{
  if (importing_ == file) {
    importing_.clear();
  }
  if (success) {
    failed_.erase(file);
  } else {
    failed_.insert(file);
  }
}

auto ContentModelCatalog::FormatsOf(const std::filesystem::path& file)
  -> Formats
{
  const auto extension = LowerExtension(file);
  return {
    .fbx = extension == kFbxExtension,
    .glb = extension == kGlbExtension,
    .gltf = extension == kGltfExtension,
  };
}

auto ContentModelCatalog::Matches(
  const ModelEntry& model, const TextFilter& filter) -> bool
{
  return filter.Matches(model.mount_path);
}

} // namespace oxygen::examples::ui

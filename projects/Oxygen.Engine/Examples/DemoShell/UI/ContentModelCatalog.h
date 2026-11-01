//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include "DemoShell/UI/ContentTextMatch.h"

#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::examples::ui {

//! The identity of an ad-hoc model import: its source's mount path under
//! `/Scratch`, without the `Content/SourceMedia` folders a cooked identity
//! never holds. Models of one file name in two folders stay two models, and
//! none shadows a committed model; its products are named under this path.
[[nodiscard]] auto ScratchModelVirtualPath(std::string_view source_mount_path)
  -> std::string;

//! The scratch project's mounts: its own `Scratch` mount, where its records
//! live, the examples' read-only `Content` mount, and the local mounts a
//! developer's `.mounts.json` files at or above the scratch project add, such
//! as a downloads folder.
/*!
 @throw std::runtime_error When the table is invalid.
*/
[[nodiscard]] auto MakeScratchMounts(const ContentRootPaths& roots)
  -> content::import::MountTable;

//! Where a model stands, as its row on the Import tab shows it.
enum class ModelState : uint8_t {
  kNotImported,
  kImported,
  //! The record's settings differ from the panel's.
  kSettingsChanged,
  //! This session's last import of it failed.
  kFailed,
  kImporting,
};

[[nodiscard]] auto to_string(ModelState state) noexcept -> std::string_view;

//! A model file of a source mount.
struct ModelEntry final {
  std::filesystem::path file;
  //! Its mount path, such as `/Content/SourceMedia/DCC/dino-a.fbx`.
  std::string mount_path;
  //! The folders between its mount and the file, such as `SourceMedia/DCC`:
  //! the band it is listed under.
  std::string folder;
  content::import::ImportFormat format { content::import::ImportFormat::kGltf };
  //! The model's identity in the scratch project.
  std::string identity;
  //! Its record, which need not exist.
  std::filesystem::path record;
  ModelState state { ModelState::kNotImported };
};

//! What the Import tab shows of the selected model's record.
struct ModelRecordView final {
  std::filesystem::path record;
  std::string source;
  //! When its selected generation was published; none before the first.
  std::optional<std::filesystem::file_time_type> published;
  std::filesystem::path generation;
  //! The scene its selected generation holds, by virtual path.
  std::string scene;
  //! The groups whose panel values differ from the record's.
  std::vector<ImportSettingsGroup> differing;
  //! The record's own settings.
  std::string settings_json;
  //! Why the record cannot be read; empty when it was.
  std::string error;
};

//! A `Source` mount of the scratch project models are picked from.
struct SourceMountEntry final {
  std::string name;
  std::filesystem::path folder;
  //! The `.mounts.json` that adds a local mount; empty for a declared one.
  std::filesystem::path mounts_file;
};

//! The extensions of the model files the Import tab lists, lowercase.
inline constexpr auto kFbxExtension = std::string_view { ".fbx" };
inline constexpr auto kGlbExtension = std::string_view { ".glb" };
inline constexpr auto kGltfExtension = std::string_view { ".gltf" };

//! The model files of a source mount and the state of each one's record.
class ContentModelCatalog final {
public:
  //! The formats the list shows.
  struct Formats final {
    bool fbx { true };
    bool glb { true };
    bool gltf { true };
  };

  auto SetMounts(content::import::MountTable mounts) -> void;

  [[nodiscard]] auto Mounts() const -> const content::import::MountTable*;

  //! The bound `Source` mounts, by name.
  [[nodiscard]] auto SourceMounts() const -> std::vector<SourceMountEntry>;

  //! Lists the model files of the mount `name`; none for an unknown mount.
  auto Scan(std::string_view name, Formats formats) -> void;

  //! Sets every model's state from its record, its last import and
  //! `panel_settings` (`ModelImportRecord::MakeSettings` of the panel's).
  auto Refresh(std::string_view panel_settings,
    const content::import::ModelImportGenerations& generations) -> void;

  [[nodiscard]] auto Models() const noexcept -> const std::vector<ModelEntry>&
  {
    return models_;
  }

  [[nodiscard]] auto Find(const std::filesystem::path& file) const
    -> const ModelEntry*;

  //! Reads the record of `model` for the details of the selected model.
  [[nodiscard]] auto ReadRecord(const ModelEntry& model,
    std::string_view panel_settings,
    const content::import::ModelImportGenerations& generations) const
    -> ModelRecordView;

  auto SetImporting(const std::filesystem::path& file) -> void;
  auto SetImportFinished(const std::filesystem::path& file, bool success)
    -> void;

  //! True when `filter` is empty or the model's mount path contains it.
  //! The formats that list `file`: its own format alone, none for a file
  //! of another kind.
  [[nodiscard]] static auto FormatsOf(const std::filesystem::path& file)
    -> Formats;

  [[nodiscard]] static auto Matches(
    const ModelEntry& model, const TextFilter& filter) -> bool;

private:
  std::optional<content::import::MountTable> mounts_;
  std::vector<ModelEntry> models_;
  std::set<std::filesystem::path> failed_;
  std::filesystem::path importing_;
};

} // namespace oxygen::examples::ui

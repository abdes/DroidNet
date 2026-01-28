//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <system_error>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/AssetType.h>

#include "DemoShell/UI/PakLoaderPanel.h"

namespace oxygen::examples::ui {

struct PakLoaderPanel::Impl {
  PakLoaderConfig config {};
  observer_ptr<FileBrowserService> file_browser { nullptr };
  std::unique_ptr<content::PakFile> pak_file {};
  std::vector<SceneListItem> scenes {};
  std::filesystem::path loaded_pak_path {};
  std::vector<std::filesystem::path> cached_pak_files {};
  bool files_cached { false };
};

PakLoaderPanel::PakLoaderPanel()
  : impl_(std::make_unique<Impl>())
{
}

PakLoaderPanel::~PakLoaderPanel() = default;

void PakLoaderPanel::Initialize(const PakLoaderConfig& config)
{
  impl_->config = config;
  CHECK_NOTNULL_F(impl_->config.file_browser_service,
    "PakLoaderPanel requires a FileBrowserService");
  impl_->file_browser = impl_->config.file_browser_service;
  impl_->files_cached = false;
  impl_->cached_pak_files.clear();
  UnloadPak();
}

auto PakLoaderPanel::EnumeratePakFiles() const
  -> std::vector<std::filesystem::path>
{
  std::vector<std::filesystem::path> files;

  std::error_code ec;
  if (!std::filesystem::exists(impl_->config.pak_directory, ec)
    || !std::filesystem::is_directory(impl_->config.pak_directory, ec)) {
    return files;
  }

  for (const auto& entry :
    std::filesystem::directory_iterator(impl_->config.pak_directory, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto path = entry.path();
    if (path.extension() == ".pak") {
      files.push_back(path);
    }
  }

  std::sort(files.begin(), files.end(),
    [](const std::filesystem::path& a, const std::filesystem::path& b) {
      return a.filename().string() < b.filename().string();
    });

  return files;
}

void PakLoaderPanel::LoadPakFile(const std::filesystem::path& pak_path)
{
  UnloadPak();

  try {
    impl_->pak_file = std::make_unique<content::PakFile>(pak_path);
    impl_->loaded_pak_path = pak_path;

    // Extract scene list from browse index
    if (impl_->pak_file->HasBrowseIndex()) {
      for (const auto& browse_entry : impl_->pak_file->BrowseIndex()) {
        const auto entry = impl_->pak_file->FindEntry(browse_entry.asset_key);
        if (!entry) {
          continue;
        }

        if (entry->asset_type
          != static_cast<uint8_t>(data::AssetType::kScene)) {
          continue;
        }

        impl_->scenes.push_back(SceneListItem {
          .virtual_path = browse_entry.virtual_path,
          .key = browse_entry.asset_key,
        });
      }

      std::sort(impl_->scenes.begin(), impl_->scenes.end(),
        [](const SceneListItem& a, const SceneListItem& b) {
          return a.virtual_path < b.virtual_path;
        });

      LOG_F(INFO, "Loaded PAK file with {} scenes: {}", impl_->scenes.size(),
        pak_path.string());
    }

    // Notify mount callback
    if (impl_->config.on_pak_mounted) {
      impl_->config.on_pak_mounted(pak_path);
    }

  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to load PAK file: {}", e.what());
    UnloadPak();
  }
}

void PakLoaderPanel::UnloadPak()
{
  impl_->pak_file.reset();
  impl_->scenes.clear();
  impl_->loaded_pak_path.clear();
}

auto PakLoaderPanel::GetScenes() const -> const std::vector<SceneListItem>&
{
  return impl_->scenes;
}

auto PakLoaderPanel::HasLoadedPak() const -> bool
{
  return impl_->pak_file != nullptr;
}

void PakLoaderPanel::Draw()
{
  // Cache file list on first draw
  if (!impl_->files_cached) {
    impl_->cached_pak_files = EnumeratePakFiles();
    impl_->files_cached = true;
  }

  // File picker and controls
  if (ImGui::Button("Browse for PAK...")) {
    const auto roots = impl_->file_browser->GetContentRoots();
    auto picker_config = MakePakFileBrowserConfig(roots);
    picker_config.initial_directory = impl_->config.pak_directory;
    impl_->file_browser->Open(picker_config);
  }
  ImGui::SameLine();

  impl_->file_browser->UpdateAndDraw();
  if (const auto selected_path = impl_->file_browser->ConsumeSelection()) {
    LoadPakFile(*selected_path);
    return;
  }

  if (ImGui::Button("Refresh List")) {
    impl_->files_cached = false;
  }

  if (HasLoadedPak()) {
    ImGui::SameLine();
    if (ImGui::Button("Unload PAK")) {
      UnloadPak();
    }
  }

  ImGui::Separator();

  // Show loaded PAK info
  if (HasLoadedPak()) {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Loaded PAK:");
    ImGui::SameLine();
    ImGui::TextUnformatted(impl_->loaded_pak_path.filename().string().c_str());

    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", impl_->loaded_pak_path.string().c_str());
    }

    ImGui::Text("Scenes: %zu", impl_->scenes.size());
    ImGui::Separator();

    // Scene selection list - stretch to fill available space
    const float available_height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginListBox("##PakScenes", ImVec2(-1.0f, available_height))) {
      for (const auto& scene_item : impl_->scenes) {
        if (ImGui::Selectable(scene_item.virtual_path.c_str(), false)) {
          // Re-mount the selected PAK before loading to avoid ambiguous asset
          // resolution when the same AssetKey exists in multiple sources.
          if (impl_->config.on_pak_mounted && HasLoadedPak()) {
            impl_->config.on_pak_mounted(impl_->loaded_pak_path);
          }
          if (impl_->config.on_scene_selected) {
            impl_->config.on_scene_selected(scene_item.key);
          }
        }

        // Tooltip with asset key
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip(
            "Key: %s", oxygen::data::to_string(scene_item.key).c_str());
        }
      }
      ImGui::EndListBox();
    }

  } else {
    // PAK file selection list - stretch to fill available space
    ImGui::TextUnformatted("Select PAK File:");

    const float available_height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginListBox("##PakFiles", ImVec2(-1.0f, available_height))) {
      for (const auto& pak_path : impl_->cached_pak_files) {
        const auto filename = pak_path.filename().string();

        if (ImGui::Selectable(filename.c_str(), false)) {
          LoadPakFile(pak_path);
        }

        // Tooltip with full path
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", pak_path.string().c_str());
        }
      }
      ImGui::EndListBox();
    }

    if (impl_->cached_pak_files.empty()) {
      ImGui::TextDisabled("No PAK files found in directory");
      ImGui::TextDisabled(
        "Directory: %s", impl_->config.pak_directory.string().c_str());
    }
  }
}

} // namespace oxygen::examples::ui

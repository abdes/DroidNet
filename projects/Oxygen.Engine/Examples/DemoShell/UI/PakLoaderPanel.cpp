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

void PakLoaderPanel::Initialize(const PakLoaderConfig& config)
{
  config_ = config;
  CHECK_NOTNULL_F(config_.file_browser_service,
    "PakLoaderPanel requires a FileBrowserService");
  file_browser_ = config_.file_browser_service;
  files_cached_ = false;
  cached_pak_files_.clear();
  UnloadPak();
}

auto PakLoaderPanel::EnumeratePakFiles() const
  -> std::vector<std::filesystem::path>
{
  std::vector<std::filesystem::path> files;

  std::error_code ec;
  if (!std::filesystem::exists(config_.pak_directory, ec)
    || !std::filesystem::is_directory(config_.pak_directory, ec)) {
    return files;
  }

  for (const auto& entry :
    std::filesystem::directory_iterator(config_.pak_directory, ec)) {
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
    pak_file_ = std::make_unique<content::PakFile>(pak_path);
    loaded_pak_path_ = pak_path;

    // Extract scene list from browse index
    if (pak_file_->HasBrowseIndex()) {
      for (const auto& browse_entry : pak_file_->BrowseIndex()) {
        const auto entry = pak_file_->FindEntry(browse_entry.asset_key);
        if (!entry) {
          continue;
        }

        if (entry->asset_type
          != static_cast<uint8_t>(data::AssetType::kScene)) {
          continue;
        }

        scenes_.push_back(SceneListItem {
          .virtual_path = browse_entry.virtual_path,
          .key = browse_entry.asset_key,
        });
      }

      std::sort(scenes_.begin(), scenes_.end(),
        [](const SceneListItem& a, const SceneListItem& b) {
          return a.virtual_path < b.virtual_path;
        });

      LOG_F(INFO, "Loaded PAK file with {} scenes: {}", scenes_.size(),
        pak_path.string());
    }

    // Notify mount callback
    if (config_.on_pak_mounted) {
      config_.on_pak_mounted(pak_path);
    }

  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to load PAK file: {}", e.what());
    UnloadPak();
  }
}

void PakLoaderPanel::UnloadPak()
{
  pak_file_.reset();
  scenes_.clear();
  loaded_pak_path_.clear();
}

void PakLoaderPanel::Draw()
{
  // Cache file list on first draw
  if (!files_cached_) {
    cached_pak_files_ = EnumeratePakFiles();
    files_cached_ = true;
  }

  // File picker and controls
  if (ImGui::Button("Browse for PAK...")) {
    const auto roots = file_browser_->GetContentRoots();
    auto picker_config = MakePakFileBrowserConfig(roots);
    picker_config.initial_directory = config_.pak_directory;
    file_browser_->Open(picker_config);
  }
  ImGui::SameLine();

  file_browser_->UpdateAndDraw();
  if (const auto selected_path = file_browser_->ConsumeSelection()) {
    LoadPakFile(*selected_path);
    return;
  }

  if (ImGui::Button("Refresh List")) {
    files_cached_ = false;
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
    ImGui::TextUnformatted(loaded_pak_path_.filename().string().c_str());

    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", loaded_pak_path_.string().c_str());
    }

    ImGui::Text("Scenes: %zu", scenes_.size());
    ImGui::Separator();

    // Scene selection list - stretch to fill available space
    const float available_height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginListBox("##PakScenes", ImVec2(-1.0f, available_height))) {
      for (const auto& scene_item : scenes_) {
        if (ImGui::Selectable(scene_item.virtual_path.c_str(), false)) {
          // Re-mount the selected PAK before loading to avoid ambiguous asset
          // resolution when the same AssetKey exists in multiple sources.
          if (config_.on_pak_mounted && HasLoadedPak()) {
            config_.on_pak_mounted(loaded_pak_path_);
          }
          if (config_.on_scene_selected) {
            config_.on_scene_selected(scene_item.key);
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
      for (const auto& pak_path : cached_pak_files_) {
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

    if (cached_pak_files_.empty()) {
      ImGui::TextDisabled("No PAK files found in directory");
      ImGui::TextDisabled(
        "Directory: %s", config_.pak_directory.string().c_str());
    }
  }
}

} // namespace oxygen::examples::ui

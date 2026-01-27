//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderScene/UI/LooseCookedLoaderPanel.h"

#include <algorithm>
#include <system_error>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>

#include "RenderScene/UI/FilePicker.h"

namespace oxygen::examples::render_scene::ui {

void LooseCookedLoaderPanel::Initialize(const LooseCookedLoaderConfig& config)
{
  config_ = config;
  auto_load_attempted_ = false;
  UnloadIndex();

  // Attempt auto-load on initialization
  TryAutoLoad();
}

void LooseCookedLoaderPanel::LoadIndexFile(
  const std::filesystem::path& index_path)
{
  UnloadIndex();

  try {
    inspection_ = std::make_unique<content::LooseCookedInspection>();
    inspection_->LoadFromFile(index_path);
    loaded_index_path_ = index_path;

    // Extract scene list
    for (const auto& asset : inspection_->Assets()) {
      if (asset.asset_type != static_cast<uint8_t>(data::AssetType::kScene)) {
        continue;
      }

      scenes_.push_back(LooseCookedSceneItem {
        .virtual_path = asset.virtual_path,
        .key = asset.key,
      });
    }

    std::sort(scenes_.begin(), scenes_.end(),
      [](const LooseCookedSceneItem& a, const LooseCookedSceneItem& b) {
        return a.virtual_path < b.virtual_path;
      });

    LOG_F(INFO, "Loaded loose cooked index with {} scenes: {}", scenes_.size(),
      index_path.string());

    // Notify load callback
    if (config_.on_index_loaded) {
      config_.on_index_loaded(index_path);
    }

  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to load loose cooked index: {}", e.what());
    UnloadIndex();
  }
}

auto LooseCookedLoaderPanel::TryAutoLoad() -> bool
{
  if (auto_load_attempted_) {
    return false;
  }

  auto_load_attempted_ = true;

  std::error_code ec;
  const auto cooked_dir
    = std::filesystem::absolute(config_.cooked_directory, ec);
  if (ec) {
    return false;
  }

  if (!std::filesystem::exists(cooked_dir, ec)
    || !std::filesystem::is_directory(cooked_dir, ec)) {
    return false;
  }

  const auto index_path = cooked_dir / "container.index.bin";
  if (!std::filesystem::exists(index_path, ec)
    || !std::filesystem::is_regular_file(index_path, ec)) {
    return false;
  }

  LoadIndexFile(index_path);
  return HasLoadedIndex();
}

void LooseCookedLoaderPanel::UnloadIndex()
{
  inspection_.reset();
  scenes_.clear();
  loaded_index_path_.clear();
}

void LooseCookedLoaderPanel::Draw()
{
  // Auto-load status
  if (!auto_load_attempted_) {
    if (ImGui::Button("Auto-Load from .cooked")) {
      TryAutoLoad();
    }
    ImGui::SameLine();
  }

  // File picker
#if defined(OXYGEN_WINDOWS)
  if (ImGui::Button("Browse for Index...")) {
    auto picker_config = MakeLooseCookedIndexPickerConfig();
    picker_config.initial_directory = config_.cooked_directory;

    if (const auto selected_path = ShowFilePicker(picker_config)) {
      LoadIndexFile(*selected_path);
      return;
    }
  }
#endif

  if (HasLoadedIndex()) {
    ImGui::SameLine();
    if (ImGui::Button("Unload Index")) {
      UnloadIndex();
    }
  }

  ImGui::Separator();

  // Show loaded index info
  if (HasLoadedIndex()) {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Loaded Index:");
    ImGui::SameLine();
    ImGui::TextUnformatted(loaded_index_path_.filename().string().c_str());

    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", loaded_index_path_.string().c_str());
    }

    ImGui::Text("Scenes: %zu", scenes_.size());
    ImGui::Text("Total Assets: %zu", inspection_->Assets().size());
    ImGui::Separator();

    // Scene selection list - stretch to fill available space
    const float available_height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginListBox(
          "##LooseCookedScenes", ImVec2(-1.0f, available_height))) {
      for (const auto& scene_item : scenes_) {
        if (ImGui::Selectable(scene_item.virtual_path.c_str(), false)) {
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

    if (scenes_.empty()) {
      ImGui::TextDisabled("No scenes found in index");
    }

  } else {
    ImGui::TextDisabled("No index loaded");
    ImGui::TextDisabled("Expected location: %s",
      (config_.cooked_directory / "container.index.bin").string().c_str());

    std::error_code ec;
    const bool cooked_exists
      = std::filesystem::exists(config_.cooked_directory, ec);

    if (!cooked_exists) {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
        "Warning: .cooked directory does not exist");
    }
  }
}

} // namespace oxygen::examples::render_scene::ui

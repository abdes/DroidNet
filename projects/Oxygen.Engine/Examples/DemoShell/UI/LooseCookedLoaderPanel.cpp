//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <system_error>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>

#include "DemoShell/UI/LooseCookedLoaderPanel.h"

namespace oxygen::examples::ui {

struct LooseCookedLoaderPanel::Impl {
  LooseCookedLoaderConfig config {};
  observer_ptr<FileBrowserService> file_browser { nullptr };
  std::unique_ptr<content::LooseCookedInspection> inspection {};
  std::vector<LooseCookedSceneItem> scenes {};
  std::filesystem::path loaded_index_path {};
  bool auto_load_attempted { false };
};

LooseCookedLoaderPanel::LooseCookedLoaderPanel()
  : impl_(std::make_unique<Impl>())
{
}

LooseCookedLoaderPanel::~LooseCookedLoaderPanel() = default;

void LooseCookedLoaderPanel::Initialize(const LooseCookedLoaderConfig& config)
{
  impl_->config = config;
  CHECK_NOTNULL_F(impl_->config.file_browser_service,
    "LooseCookedLoaderPanel requires a FileBrowserService");
  impl_->file_browser = impl_->config.file_browser_service;
  impl_->auto_load_attempted = false;
  UnloadIndex();

  // Attempt auto-load on initialization
  TryAutoLoad();
}

void LooseCookedLoaderPanel::LoadIndexFile(
  const std::filesystem::path& index_path)
{
  UnloadIndex();

  try {
    impl_->inspection = std::make_unique<content::LooseCookedInspection>();
    impl_->inspection->LoadFromFile(index_path);
    impl_->loaded_index_path = index_path;

    // Extract scene list
    for (const auto& asset : impl_->inspection->Assets()) {
      if (asset.asset_type != static_cast<uint8_t>(data::AssetType::kScene)) {
        continue;
      }

      impl_->scenes.push_back(LooseCookedSceneItem {
        .virtual_path = asset.virtual_path,
        .key = asset.key,
      });
    }

    std::sort(impl_->scenes.begin(), impl_->scenes.end(),
      [](const LooseCookedSceneItem& a, const LooseCookedSceneItem& b) {
        return a.virtual_path < b.virtual_path;
      });

    LOG_F(INFO, "Loaded loose cooked index with {} scenes: {}",
      impl_->scenes.size(), index_path.string());

    // Notify load callback
    if (impl_->config.on_index_loaded) {
      impl_->config.on_index_loaded(index_path);
    }

  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to load loose cooked index: {}", e.what());
    UnloadIndex();
  }
}

auto LooseCookedLoaderPanel::TryAutoLoad() -> bool
{
  if (impl_->auto_load_attempted) {
    return false;
  }

  impl_->auto_load_attempted = true;

  std::error_code ec;
  const auto cooked_dir
    = std::filesystem::absolute(impl_->config.cooked_directory, ec);
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
  impl_->inspection.reset();
  impl_->scenes.clear();
  impl_->loaded_index_path.clear();
}

auto LooseCookedLoaderPanel::GetScenes() const
  -> const std::vector<LooseCookedSceneItem>&
{
  return impl_->scenes;
}

auto LooseCookedLoaderPanel::HasLoadedIndex() const -> bool
{
  return impl_->inspection != nullptr;
}

void LooseCookedLoaderPanel::Draw()
{
  // Auto-load status
  if (!impl_->auto_load_attempted) {
    if (ImGui::Button("Auto-Load from .cooked")) {
      TryAutoLoad();
    }
    ImGui::SameLine();
  }

  // File picker
  if (ImGui::Button("Browse for Index...")) {
    const auto roots = impl_->file_browser->GetContentRoots();
    auto picker_config = MakeLooseCookedIndexBrowserConfig(roots);
    picker_config.initial_directory = impl_->config.cooked_directory;
    impl_->file_browser->Open(picker_config);
  }

  impl_->file_browser->UpdateAndDraw();
  if (const auto selected_path = impl_->file_browser->ConsumeSelection()) {
    LoadIndexFile(*selected_path);
    return;
  }

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
    ImGui::TextUnformatted(
      impl_->loaded_index_path.filename().string().c_str());

    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", impl_->loaded_index_path.string().c_str());
    }

    ImGui::Text("Scenes: %zu", impl_->scenes.size());
    ImGui::Text("Total Assets: %zu", impl_->inspection->Assets().size());
    ImGui::Separator();

    // Scene selection list - stretch to fill available space
    const float available_height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginListBox(
          "##LooseCookedScenes", ImVec2(-1.0f, available_height))) {
      for (const auto& scene_item : impl_->scenes) {
        if (ImGui::Selectable(scene_item.virtual_path.c_str(), false)) {
          // Re-mount the selected cooked root before loading to avoid
          // ambiguous asset resolution across multiple sources.
          if (impl_->config.on_index_loaded && HasLoadedIndex()) {
            impl_->config.on_index_loaded(impl_->loaded_index_path);
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

    if (impl_->scenes.empty()) {
      ImGui::TextDisabled("No scenes found in index");
    }

  } else {
    ImGui::TextDisabled("No index loaded");
    ImGui::TextDisabled("Expected location: %s",
      (impl_->config.cooked_directory / "container.index.bin")
        .string()
        .c_str());

    std::error_code ec;
    const bool cooked_exists
      = std::filesystem::exists(impl_->config.cooked_directory, ec);

    if (!cooked_exists) {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
        "Warning: .cooked directory does not exist");
    }
  }
}

} // namespace oxygen::examples::ui

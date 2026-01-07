//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "FbxLoaderPanel.h"

#include <algorithm>
#include <system_error>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>

#include "FilePicker.h"

namespace oxygen::examples::render_scene::ui {

void FbxLoaderPanel::Initialize(const FbxLoaderConfig& config)
{
  config_ = config;
  files_cached_ = false;
  cached_fbx_files_.clear();
}

auto FbxLoaderPanel::EnumerateFbxFiles() const
  -> std::vector<std::filesystem::path>
{
  std::vector<std::filesystem::path> files;

  std::error_code ec;
  if (!std::filesystem::exists(config_.fbx_directory, ec)
    || !std::filesystem::is_directory(config_.fbx_directory, ec)) {
    return files;
  }

  for (const auto& entry :
    std::filesystem::directory_iterator(config_.fbx_directory, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto path = entry.path();
    if (path.extension() == ".fbx") {
      files.push_back(path);
    }
  }

  std::sort(files.begin(), files.end(),
    [](const std::filesystem::path& a, const std::filesystem::path& b) {
      return a.filename().string() < b.filename().string();
    });

  return files;
}

void FbxLoaderPanel::StartImport(const std::filesystem::path& fbx_path)
{
  import_state_.importing_path = fbx_path.string();
  import_state_.is_importing = true;

  const auto cooked_root
    = std::filesystem::absolute(config_.cooked_output_directory);

  import_state_.import_future = std::async(std::launch::async,
    [fbx_path, cooked_root]() -> std::optional<data::AssetKey> {
      std::error_code ec;
      (void)std::filesystem::create_directories(cooked_root, ec);

      auto importer = std::make_unique<content::import::AssetImporter>();

      try {
        content::import::ImportRequest request {};
        request.source_path = fbx_path;
        request.cooked_root = cooked_root;
        request.options.naming_strategy
          = std::make_shared<content::import::NormalizeNamingStrategy>();

        (void)importer->ImportToLooseCooked(request);

        auto inspection = std::make_unique<content::LooseCookedInspection>();
        const auto index_path
          = cooked_root / request.loose_cooked_layout.index_file_name;
        inspection->LoadFromFile(index_path);

        const auto expected_scene_name = fbx_path.stem().string();
        const auto expected_virtual_path
          = request.loose_cooked_layout.SceneVirtualPath(expected_scene_name);

        std::optional<data::AssetKey> matching_scene_key;
        std::optional<data::AssetKey> first_scene_key;
        std::string first_scene_path;

        for (const auto& asset : inspection->Assets()) {
          if (asset.asset_type
            != static_cast<uint8_t>(data::AssetType::kScene)) {
            continue;
          }

          if (asset.virtual_path == expected_virtual_path) {
            matching_scene_key = asset.key;
          }

          if (!first_scene_key || asset.virtual_path < first_scene_path) {
            first_scene_key = asset.key;
            first_scene_path = asset.virtual_path;
          }
        }

        return matching_scene_key ? matching_scene_key : first_scene_key;

      } catch (const std::exception& e) {
        LOG_F(ERROR, "FBX import failed: {}", e.what());
        return std::nullopt;
      }
    });
}

void FbxLoaderPanel::Update()
{
  if (!import_state_.import_future.valid()) {
    return;
  }

  if (import_state_.import_future.wait_for(std::chrono::seconds(0))
    != std::future_status::ready) {
    return;
  }

  // Import completed
  import_state_.is_importing = false;
  const auto result = import_state_.import_future.get();

  if (result && config_.on_scene_ready) {
    config_.on_scene_ready(*result);
  } else if (!result) {
    LOG_F(ERROR, "FBX import failed or produced no scene");
  }
}

void FbxLoaderPanel::Draw()
{
  if (import_state_.is_importing) {
    ImGui::Text("Importing FBX: %s", import_state_.importing_path.c_str());

    // Animated progress bar - stretch to fill available width
    const float time = static_cast<float>(ImGui::GetTime());
    ImGui::ProgressBar(
      -1.0f * time * 0.2f, ImVec2(-1.0f, 0.0f), "Importing...");
    return;
  }

  // Cache file list on first draw or when requested
  if (!files_cached_) {
    cached_fbx_files_ = EnumerateFbxFiles();
    files_cached_ = true;
  }

  // File picker button
#if defined(OXYGEN_WINDOWS)
  if (ImGui::Button("Browse for FBX...")) {
    auto picker_config = MakeFbxFilePickerConfig();
    picker_config.initial_directory = config_.fbx_directory;

    if (const auto selected_path = ShowFilePicker(picker_config)) {
      StartImport(*selected_path);
      return;
    }
  }
  ImGui::SameLine();
#endif
  if (ImGui::Button("Refresh List")) {
    files_cached_ = false;
  }

  // FBX files list - stretch to fill available space
  const float available_height = ImGui::GetContentRegionAvail().y;
  if (ImGui::BeginListBox("##FbxFiles", ImVec2(-1.0f, available_height))) {
    for (const auto& fbx_path : cached_fbx_files_) {
      const auto filename = fbx_path.filename().string();

      if (ImGui::Selectable(filename.c_str(), false)) {
        StartImport(fbx_path);
      }

      // Tooltip with full path
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", fbx_path.string().c_str());
      }
    }
    ImGui::EndListBox();
  }

  if (cached_fbx_files_.empty()) {
    ImGui::TextDisabled("No FBX files found in directory");
    ImGui::TextDisabled(
      "Directory: %s", config_.fbx_directory.string().c_str());
  }
}

} // namespace oxygen::examples::render_scene::ui

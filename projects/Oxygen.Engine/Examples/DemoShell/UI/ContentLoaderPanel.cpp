//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>

#include "DemoShell/UI/ContentLoaderPanel.h"

namespace oxygen::examples::ui {

void ContentLoaderPanel::Initialize(const Config& config)
{
  CHECK_NOTNULL_F(config.file_browser_service,
    "ContentLoaderPanel requires a FileBrowserService");
  const auto roots = config.file_browser_service->GetContentRoots();

  // Configure unified import panel
  ImportPanelConfig import_config;
  import_config.fbx_directory = roots.fbx_directory;
  import_config.gltf_directory = roots.glb_directory;
  import_config.cooked_output_directory = roots.cooked_root;
  import_config.file_browser_service = config.file_browser_service;
  import_config.on_scene_ready = config.on_scene_load_requested;
  import_config.on_index_loaded = config.on_loose_index_loaded;
  import_config.on_dump_texture_memory = config.on_dump_texture_memory;
  import_panel_.Initialize(import_config);

  // Configure PAK loader panel
  PakLoaderConfig pak_config;
  pak_config.pak_directory = roots.pak_directory;
  pak_config.file_browser_service = config.file_browser_service;
  pak_config.on_scene_selected = config.on_scene_load_requested;
  pak_config.on_pak_mounted = config.on_pak_mounted;
  pak_panel_.Initialize(pak_config);

  // Configure loose cooked loader panel
  LooseCookedLoaderConfig loose_config;
  loose_config.cooked_directory = roots.cooked_root;
  loose_config.file_browser_service = config.file_browser_service;
  loose_config.on_scene_selected = config.on_scene_load_requested;
  loose_config.on_index_loaded = config.on_loose_index_loaded;
  loose_cooked_panel_.Initialize(loose_config);

  get_last_released_scene_key_ = config.get_last_released_scene_key;
  on_force_trim_ = config.on_force_trim;
}

void ContentLoaderPanel::Update()
{
  // Import panel needs update for async import status
  import_panel_.Update();
}

void ContentLoaderPanel::Draw()
{
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(540, 350), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Content Loader", nullptr, ImGuiWindowFlags_None)) {
    ImGui::End();
    return;
  }

  DrawContents();
  ImGui::End();
}

void ContentLoaderPanel::DrawContents()
{
  if (get_last_released_scene_key_) {
    const auto last_key = get_last_released_scene_key_();
    if (last_key.has_value()) {
      ImGui::Text(
        "Last released scene: %s", oxygen::data::to_string(*last_key).c_str());
    } else {
      ImGui::TextDisabled("Last released scene: <none>");
    }
  }

  if (on_force_trim_) {
    if (ImGui::Button("Force Trim")) {
      on_force_trim_();
    }
  }

  if (get_last_released_scene_key_ || on_force_trim_) {
    ImGui::Separator();
  }

  if (ImGui::BeginTabBar("ContentSourceTabs")) {
    if (ImGui::BeginTabItem("Import")) {
      import_panel_.Draw();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("PAK")) {
      pak_panel_.Draw();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Loose Cooked")) {
      loose_cooked_panel_.Draw();
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }
}

} // namespace oxygen::examples::ui

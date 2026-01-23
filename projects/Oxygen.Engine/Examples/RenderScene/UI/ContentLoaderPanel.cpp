//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ContentLoaderPanel.h"

#include <imgui.h>

namespace oxygen::examples::render_scene::ui {

void ContentLoaderPanel::Initialize(const Config& config)
{
  // Configure unified import panel
  ImportPanelConfig import_config;
  import_config.fbx_directory = config.content_root / "fbx";
  import_config.gltf_directory = config.content_root / "glb";
  import_config.cooked_output_directory = config.content_root / ".cooked";
  import_config.on_scene_ready = config.on_scene_load_requested;
  import_config.on_index_loaded = config.on_loose_index_loaded;
  import_config.on_dump_texture_memory = config.on_dump_texture_memory;
  import_panel_.Initialize(import_config);

  // Configure PAK loader panel
  PakLoaderConfig pak_config;
  pak_config.pak_directory = config.content_root / "pak";
  pak_config.on_scene_selected = config.on_scene_load_requested;
  pak_config.on_pak_mounted = config.on_pak_mounted;
  pak_panel_.Initialize(pak_config);

  // Configure loose cooked loader panel
  LooseCookedLoaderConfig loose_config;
  loose_config.cooked_directory = config.content_root / ".cooked";
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

  ImGui::End();
}

} // namespace oxygen::examples::render_scene::ui

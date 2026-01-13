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
  // Configure FBX loader panel
  FbxLoaderConfig fbx_config;
  fbx_config.fbx_directory = config.content_root / "fbx";
  fbx_config.cooked_output_directory = config.content_root / ".cooked";
  fbx_config.on_scene_ready = config.on_scene_load_requested;
  fbx_config.on_index_loaded = config.on_loose_index_loaded;
  fbx_config.on_dump_texture_memory = config.on_dump_texture_memory;
  fbx_panel_.Initialize(fbx_config);

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
}

void ContentLoaderPanel::Update()
{
  // FBX panel needs update for async import status
  fbx_panel_.Update();
}

void ContentLoaderPanel::Draw()
{
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(540, 350), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Content Loader", nullptr, ImGuiWindowFlags_None)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabBar("ContentSourceTabs")) {
    if (ImGui::BeginTabItem("FBX")) {
      fbx_panel_.Draw();
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

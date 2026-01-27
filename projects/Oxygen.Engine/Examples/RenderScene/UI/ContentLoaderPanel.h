//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "RenderScene/UI/CameraControlPanel.h"
#include "RenderScene/UI/ImportPanel.h"
#include "RenderScene/UI/LooseCookedLoaderPanel.h"
#include "RenderScene/UI/PakLoaderPanel.h"

namespace oxygen::examples::render_scene::ui {

//! Unified content loader panel combining all loading options
/*!
 Provides a single ImGui window with tabs for Import, PAK, and Loose Cooked
 content loading. Manages all loader panels internally and provides a clean
 interface for scene loading operations.

 ### Key Features

 - **Unified Interface:** Single window with tabbed content sources
 - **Auto-Initialization:** Automatically configures all sub-panels
 - **Integrated Callbacks:** Single callback for scene loading
 - **Modular Architecture:** Each loader in separate panel class

 ### Usage Examples

 ```cpp
 ContentLoaderPanel loader_panel;

 ContentLoaderConfig config;
 config.content_root = FindRenderSceneContentRoot();
 config.on_scene_load_requested = [this](const data::AssetKey& key) {
   pending_scene_key_ = key;
   pending_load_scene_ = true;
 };
 config.on_pak_mounted = [this](const std::filesystem::path& path) {
   auto loader = app_.engine->GetAssetLoader();
   loader->ClearMounts();
   loader->AddPakFile(path);
 };
 config.on_loose_index_loaded = [this](const std::filesystem::path& path) {
   auto loader = app_.engine->GetAssetLoader();
   loader->ClearMounts();
   loader->AddLooseCookedRoot(path.parent_path());
 };

 loader_panel.Initialize(config);

 // In update loop (before ImGui rendering)
 loader_panel.Update();

 // In ImGui rendering
 loader_panel.Draw();
 ```

 @see ImportPanel, PakLoaderPanel, LooseCookedLoaderPanel
 */
class ContentLoaderPanel {
public:
  ContentLoaderPanel() = default;
  ~ContentLoaderPanel() = default;

  //! Configuration for content loader panel
  struct Config {
    std::filesystem::path content_root;
    SceneLoadCallback on_scene_load_requested;
    PakMountCallback on_pak_mounted;
    IndexLoadCallback on_loose_index_loaded;
    //! Optional callback to dump runtime texture memory telemetry.
    std::function<void(std::size_t)> on_dump_texture_memory;
    //! Optional callback to get the last released scene key.
    std::function<std::optional<data::AssetKey>()> get_last_released_scene_key;
    //! Optional callback to force trim content caches.
    std::function<void()> on_force_trim;
  };

  //! Initialize panel with configuration
  void Initialize(const Config& config);

  //! Update all loader panels (call before ImGui rendering)
  void Update();

  //! Draw the ImGui panel content
  void Draw();

  //! Draw the panel content without creating a window.
  void DrawContents();

  //! Get unified import panel
  [[nodiscard]] auto GetImportPanel() -> ImportPanel& { return import_panel_; }

  //! Get PAK loader panel
  [[nodiscard]] auto GetPakPanel() -> PakLoaderPanel& { return pak_panel_; }

  //! Get loose cooked loader panel
  [[nodiscard]] auto GetLooseCookedPanel() -> LooseCookedLoaderPanel&
  {
    return loose_cooked_panel_;
  }

private:
  ImportPanel import_panel_;
  PakLoaderPanel pak_panel_;
  LooseCookedLoaderPanel loose_cooked_panel_;
  std::function<std::optional<data::AssetKey>()> get_last_released_scene_key_;
  std::function<void()> on_force_trim_;
};

} // namespace oxygen::examples::render_scene::ui

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DemoShell/Services/FileBrowserService.h"

#include <Oxygen/Data/AssetKey.h>

namespace oxygen::examples::ui {

//! Scene item from loose cooked index
struct LooseCookedSceneItem {
  std::string virtual_path;
  data::AssetKey key {};
};

//! Callback invoked when a scene is selected for loading
using LooseCookedSceneSelectCallback
  = std::function<void(const data::AssetKey&)>;

//! Callback invoked when a loose cooked index is loaded
using IndexLoadCallback = std::function<void(const std::filesystem::path&)>;

//! Configuration for loose cooked loader panel
struct LooseCookedLoaderConfig {
  std::filesystem::path cooked_directory;
  observer_ptr<FileBrowserService> file_browser_service { nullptr };
  LooseCookedSceneSelectCallback on_scene_selected;
  IndexLoadCallback on_index_loaded;
};

//! Loose cooked index loader and scene browser panel
/*!
 Displays an ImGui panel for loading loose cooked index files either
 automatically from a .cooked directory or via file picker. Once loaded,
 displays available scenes from the index.

 ### Key Features

 - **Auto-Discovery:** Automatically loads index from .cooked directory
 - **File Picker Integration:** Allows manual index file selection
 - **Scene Browser:** Lists all scenes in loaded index
 - **Mount Integration:** Coordinates with asset loader system

 ### Usage Examples

 ```cpp
 LooseCookedLoaderPanel panel;
 LooseCookedLoaderConfig config;
 config.cooked_directory = cooked_root;
 FileBrowserService browser_service;
 config.file_browser_service = observer_ptr { &browser_service };
 config.on_scene_selected = [](const data::AssetKey& key) {
   StartLoadingScene(key);
 };
 config.on_index_loaded = [](const std::filesystem::path& path) {
   asset_loader->AddLooseCookedRoot(path.parent_path());
 };

 panel.Initialize(config);

 // In ImGui update loop
 panel.Draw();
 ```

 @see LooseCookedLoaderConfig, LooseCookedSceneItem
 */
class LooseCookedLoaderPanel {
public:
  LooseCookedLoaderPanel();
  ~LooseCookedLoaderPanel();

  //! Initialize panel with configuration
  void Initialize(const LooseCookedLoaderConfig& config);

  //! Draw the ImGui panel content
  /*!
   Renders the loose cooked loader UI including auto-load status,
   file picker, and scene list.

   @note Must be called within ImGui rendering context
   */
  void Draw();

  //! Attempt to auto-load index from configured directory
  /*!
   Tries to load container.index.bin from the cooked_directory.
   Called automatically during Initialize if file exists.

   @return True if index was loaded successfully
   */
  auto TryAutoLoad() -> bool;

  //! Get list of scenes in currently loaded index
  [[nodiscard]] auto GetScenes() const
    -> const std::vector<LooseCookedSceneItem>&;

  //! Check if an index file is currently loaded
  [[nodiscard]] auto HasLoadedIndex() const -> bool;

  //! Unload current index
  void UnloadIndex();

private:
  void LoadIndexFile(const std::filesystem::path& index_path);

  struct Impl;
  std::unique_ptr<Impl> impl_ {};
};

} // namespace oxygen::examples::ui

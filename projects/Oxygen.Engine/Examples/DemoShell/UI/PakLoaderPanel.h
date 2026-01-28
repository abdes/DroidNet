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

//! Scene item in PAK file browse index
struct SceneListItem {
  std::string virtual_path;
  data::AssetKey key {};
};

//! Callback invoked when a scene is selected for loading
using SceneSelectCallback = std::function<void(const data::AssetKey&)>;

//! Callback invoked when a PAK file is mounted
using PakMountCallback = std::function<void(const std::filesystem::path&)>;

//! Configuration for PAK loader panel
struct PakLoaderConfig {
  std::filesystem::path pak_directory;
  observer_ptr<FileBrowserService> file_browser_service { nullptr };
  SceneSelectCallback on_scene_selected;
  PakMountCallback on_pak_mounted;
};

//! PAK file loader and scene browser panel
/*!
 Displays an ImGui panel for loading PAK files either from a scanned directory
 or via file picker. Once loaded, displays available scenes from the PAK's
 browse index.

 ### Key Features

 - **Directory Scanning:** Auto-scans PAK directory for available files
 - **File Picker Integration:** Allows manual PAK file selection
 - **Scene Browser:** Lists all scenes in loaded PAK file
 - **Mount Integration:** Coordinates with asset loader system

 ### Usage Examples

 ```cpp
 PakLoaderPanel panel;
 PakLoaderConfig config;
 FileBrowserService browser_service;
 const auto roots = browser_service.GetContentRoots();
 config.pak_directory = roots.pak_directory;
 config.file_browser_service = observer_ptr { &browser_service };
 config.on_scene_selected = [](const data::AssetKey& key) {
   StartLoadingScene(key);
 };
 config.on_pak_mounted = [](const std::filesystem::path& path) {
   asset_loader->AddPakFile(path);
 };

 panel.Initialize(config);

 // In ImGui update loop
 panel.Draw();
 ```

 @see PakLoaderConfig, SceneListItem
 */
class PakLoaderPanel {
public:
  PakLoaderPanel();
  ~PakLoaderPanel();

  //! Initialize panel with configuration
  void Initialize(const PakLoaderConfig& config);

  //! Draw the ImGui panel content
  /*!
   Renders the PAK loader UI including file selection, mounted PAK info,
   and scene list.

   @note Must be called within ImGui rendering context
   */
  void Draw();

  //! Get list of scenes in currently loaded PAK
  [[nodiscard]] auto GetScenes() const -> const std::vector<SceneListItem>&;

  //! Check if a PAK file is currently loaded
  [[nodiscard]] auto HasLoadedPak() const -> bool;

  //! Unload current PAK file
  void UnloadPak();

private:
  void LoadPakFile(const std::filesystem::path& pak_path);
  auto EnumeratePakFiles() const -> std::vector<std::filesystem::path>;

  struct Impl;
  std::unique_ptr<Impl> impl_ {};
};

} // namespace oxygen::examples::ui

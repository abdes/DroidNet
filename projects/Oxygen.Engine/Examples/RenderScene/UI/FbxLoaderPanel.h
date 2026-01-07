//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import {
class AssetImporter;
} // namespace oxygen::content::import

namespace oxygen::examples::render_scene::ui {

//! Callback invoked when a scene is ready to be loaded
using SceneLoadCallback = std::function<void(const data::AssetKey&)>;

//! State for FBX file import operation
struct FbxImportState {
  bool is_importing { false };
  std::string importing_path;
  std::future<std::optional<data::AssetKey>> import_future;
};

//! Configuration for FBX loader panel
struct FbxLoaderConfig {
  std::filesystem::path fbx_directory;
  std::filesystem::path cooked_output_directory;
  SceneLoadCallback on_scene_ready;
};

//! FBX file loader and importer panel
/*!
 Displays an ImGui panel for loading FBX files either from a scanned directory
 or via file picker. Handles asynchronous FBX import operations and provides
 visual feedback during import.

 ### Key Features

 - **Directory Scanning:** Auto-scans FBX directory for available files
 - **File Picker Integration:** Allows manual file selection
 - **Async Import:** Non-blocking FBX import with progress indicator
 - **Scene Selection:** Presents imported scenes for loading

 ### Usage Examples

 ```cpp
 FbxLoaderPanel panel;
 FbxLoaderConfig config;
 config.fbx_directory = content_root / "fbx";
 config.cooked_output_directory = content_root / ".cooked";
 config.on_scene_ready = [](const data::AssetKey& key) {
   LoadScene(key);
 };

 panel.Initialize(config);

 // In ImGui update loop
 panel.Draw();
 ```

 @see FbxLoaderConfig, FbxImportState
 */
class FbxLoaderPanel {
public:
  FbxLoaderPanel() = default;
  ~FbxLoaderPanel() = default;

  //! Initialize panel with configuration
  void Initialize(const FbxLoaderConfig& config);

  //! Draw the ImGui panel content
  /*!
   Renders the FBX loader UI including file list, import progress,
   and file picker button.

   @note Must be called within ImGui rendering context
   */
  void Draw();

  //! Update import state (call once per frame)
  /*!
   Checks async import status and triggers scene load callback when ready.

   @warning Must be called from main thread before rendering
   */
  void Update();

  //! Check if currently importing an FBX file
  [[nodiscard]] auto IsImporting() const -> bool
  {
    return import_state_.is_importing;
  }

  //! Get path of currently importing file
  [[nodiscard]] auto GetImportingPath() const -> const std::string&
  {
    return import_state_.importing_path;
  }

private:
  void StartImport(const std::filesystem::path& fbx_path);
  auto EnumerateFbxFiles() const -> std::vector<std::filesystem::path>;

  FbxLoaderConfig config_;
  FbxImportState import_state_;
  std::vector<std::filesystem::path> cached_fbx_files_;
  bool files_cached_ { false };
};

} // namespace oxygen::examples::render_scene::ui

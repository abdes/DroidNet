//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::examples::render_scene::ui {

//! Callback invoked when a scene is ready to be loaded
using SceneLoadCallback = std::function<void(const data::AssetKey&)>;

//! Callback invoked when a loose cooked index is available
using IndexLoadCallback = std::function<void(const std::filesystem::path&)>;

//! State for FBX file import operation
struct FbxImportState {
  std::atomic_bool is_importing { false };
  std::atomic_bool cancel_requested { false };
  std::atomic_bool completion_ready { false };
  std::string importing_path;
  content::import::ImportJobId job_id = content::import::kInvalidJobId;

  std::mutex progress_mutex;
  content::import::ProgressEvent progress {};
  std::vector<content::import::ImportDiagnostic> diagnostics;

  std::mutex completion_mutex;
  std::optional<content::import::ImportReport> completion_report;
  std::string completion_error;
};

//! Configuration for FBX loader panel
struct FbxLoaderConfig {
  std::filesystem::path fbx_directory;
  std::filesystem::path cooked_output_directory;
  SceneLoadCallback on_scene_ready;
  IndexLoadCallback on_index_loaded;

  //! Optional callback to dump runtime texture memory telemetry.
  std::function<void(std::size_t)> on_dump_texture_memory;
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
    return import_state_.is_importing.load(std::memory_order_relaxed);
  }

  //! Get path of currently importing file
  [[nodiscard]] auto GetImportingPath() const -> const std::string&
  {
    return import_state_.importing_path;
  }

  //! Request cancellation of an ongoing import.
  void CancelImport();

private:
  void StartImport(const std::filesystem::path& fbx_path);
  auto EnumerateFbxFiles() const -> std::vector<std::filesystem::path>;

  auto DrawTextureTuningUi() -> void;

  FbxLoaderConfig config_;
  std::unique_ptr<content::import::AsyncImportService> import_service_;
  content::import::AsyncImportService::Config service_config_ {};
  content::import::LooseCookedLayout layout_ {};
  std::string last_import_source_;
  FbxImportState import_state_;
  std::vector<std::filesystem::path> cached_fbx_files_;
  bool files_cached_ { false };

  // Import-time texture cooking overrides.
  content::import::ImportOptions::TextureTuning texture_tuning_ {
    .enabled = true,
    .mip_policy = content::import::MipPolicy::kFullChain,
    .max_mip_levels = 10,
    .mip_filter = content::import::MipFilter::kKaiser,
    .color_output_format = Format::kBC7UNormSRGB,
    .data_output_format = Format::kBC7UNorm,
    .bc7_quality = content::import::Bc7Quality::kDefault,
    .packing_policy_id = "d3d12",
  };

  bool auto_dump_texture_memory_ { true };
  int auto_dump_delay_frames_ { 180 };
  int pending_auto_dump_frames_ { 0 };
  int dump_top_n_ { 20 };
};

} // namespace oxygen::examples::render_scene::ui

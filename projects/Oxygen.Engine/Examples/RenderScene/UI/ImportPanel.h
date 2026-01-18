//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportFormat.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::examples::render_scene::ui {

//! Callback invoked when a scene is ready to be loaded.
using SceneLoadCallback = std::function<void(const data::AssetKey&)>;

//! Callback invoked when a loose cooked index is available.
using IndexLoadCallback = std::function<void(const std::filesystem::path&)>;

//! Configuration for the unified async import panel.
struct ImportPanelConfig {
  std::filesystem::path fbx_directory;
  std::filesystem::path gltf_directory;
  std::filesystem::path cooked_output_directory;
  SceneLoadCallback on_scene_ready;
  IndexLoadCallback on_index_loaded;

  //! Optional callback to dump runtime texture memory telemetry.
  std::function<void(std::size_t)> on_dump_texture_memory;
};

//! Unified importer panel for FBX and GLB/GLTF.
/*!
 Provides a modern ImGui panel that drives the async importer for FBX and
 GLB/GLTF sources. The panel exposes both service-wide tuning and per-session
 import options while keeping a single, integrated workflow.

 ### Key Features

 - **Async Import**: Uses AsyncImportService for background processing.
 - **Unified Sources**: Single list for FBX and GLB/GLTF files.
 - **Session Tuning**: Full ImportOptions control, including texture cooking.
 - **Service Tuning**: Configure worker counts and queue capacities.

 ### Usage Examples

 ```cpp
 ImportPanel panel;
 ImportPanelConfig config;
 config.fbx_directory = content_root / "fbx";
 config.gltf_directory = content_root / "glb";
 config.cooked_output_directory = content_root / ".cooked";
 config.on_scene_ready = [](const data::AssetKey& key) { LoadScene(key); };

 panel.Initialize(config);
 panel.Update();
 panel.Draw();
 ```

 @see ImportPanelConfig
*/
class ImportPanel {
public:
  ImportPanel() = default;
  ~ImportPanel() = default;

  //! Initialize panel with configuration.
  void Initialize(const ImportPanelConfig& config);

  //! Update import state (call once per frame).
  void Update();

  //! Draw the ImGui panel content.
  void Draw();

  //! Check if an import is currently in progress.
  [[nodiscard]] auto IsImporting() const -> bool;

  //! Request cancellation of an ongoing import.
  void CancelImport();

private:
  struct SourceEntry {
    std::filesystem::path path;
    content::import::ImportFormat format
      = content::import::ImportFormat::kUnknown;
  };

  struct ImportJobState {
    std::atomic_bool is_importing { false };
    std::atomic_bool cancel_requested { false };
    std::atomic_bool completion_ready { false };
    std::string importing_path;
    content::import::ImportJobId job_id = content::import::kInvalidJobId;

    std::mutex progress_mutex;
    content::import::ImportProgress progress {};
    std::vector<content::import::ImportDiagnostic> diagnostics;

    std::mutex completion_mutex;
    std::optional<content::import::ImportReport> completion_report;
    std::string completion_error;
  };

  void StartImport(const std::filesystem::path& source_path);
  auto EnumerateSourceFiles() const -> std::vector<SourceEntry>;
  void RefreshSourceCache();

  auto DrawSourceSelectionUi() -> void;
  auto DrawSessionConfigUi() -> void;
  auto DrawImportOptionsUi() -> void;
  auto DrawTextureTuningUi() -> void;
  auto DrawOutputLayoutUi() -> void;
  auto DrawServiceConfigUi() -> void;
  auto DrawDiagnosticsUi() -> void;
  auto DrawJobSummaryUi() -> void;

  ImportPanelConfig config_ {};
  std::unique_ptr<content::import::AsyncImportService> import_service_;
  content::import::AsyncImportService::Config service_config_ {};

  content::import::ImportOptions import_options_ {};
  content::import::ImportOptions::TextureTuning texture_tuning_ {};
  content::import::LooseCookedLayout layout_ {};

  ImportJobState import_state_ {};
  std::optional<content::import::ImportReport> last_report_ {};
  std::string last_import_source_;

  std::vector<SourceEntry> cached_files_ {};
  bool files_cached_ = false;

  std::string fbx_directory_text_;
  std::string gltf_directory_text_;
  std::string cooked_output_text_;
  std::string job_name_text_;

  std::string virtual_mount_root_text_;
  std::string index_file_name_text_;
  std::string resources_dir_text_;
  std::string descriptors_dir_text_;
  std::string scenes_subdir_text_;
  std::string geometry_subdir_text_;
  std::string materials_subdir_text_;

  bool include_fbx_ = true;
  bool include_glb_ = true;
  bool include_gltf_ = true;
  bool use_cooked_root_override_ = true;
  bool use_normalize_naming_ = true;
  bool auto_load_scene_ = true;

  bool auto_dump_texture_memory_ = true;
  int auto_dump_delay_frames_ = 180;
  int pending_auto_dump_frames_ = 0;
  int dump_top_n_ = 20;

  bool pending_service_restart_ = false;
  bool service_config_dirty_ = false;
};

} // namespace oxygen::examples::render_scene::ui

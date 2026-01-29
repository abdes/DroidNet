//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/ImportPanel.h"
#include "DemoShell/UI/LooseCookedLoaderPanel.h"
#include "DemoShell/UI/PakLoaderPanel.h"

namespace oxygen::examples::ui {

//! Unified content loader panel combining all loading options
/*!
 Provides a single ImGui window with tabs for Import, PAK, and Loose Cooked
 content loading. Manages all loader panels internally and provides a clean
 interface for scene loading operations. The shared content root is resolved
 by FileBrowserService and points to Examples/Content.

 ### Key Features

 - **Unified Interface:** Single window with tabbed content sources
 - **Auto-Initialization:** Automatically configures all sub-panels
 - **Integrated Callbacks:** Single callback for scene loading
 - **Modular Architecture:** Each loader in separate panel class

 ### Usage Examples

 ```cpp
 ContentLoaderPanel loader_panel;

 ContentLoaderPanel::Config config;
 FileBrowserService browser_service;
 browser_service.ConfigureContentRoots(
   { .cooked_root = std::filesystem::path("...") / ".cooked" });
 config.file_browser_service = observer_ptr { &browser_service };
 config.cooked_root = std::filesystem::path("...") / ".cooked";
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

 // Registered with DemoShell; the shell draws DrawContents() when active.
 ```

 @see ImportPanel, PakLoaderPanel, LooseCookedLoaderPanel
 */
class ContentLoaderPanel final : public DemoPanel {
public:
  ContentLoaderPanel() = default;
  ~ContentLoaderPanel() override = default;

  //! Configuration for content loader panel
  struct Config {
    observer_ptr<FileBrowserService> file_browser_service { nullptr };
    //! Demo-specific cooked output root (e.g. "Examples/MyDemo/.cooked").
    std::filesystem::path cooked_root;
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

  //! Draw the panel content without creating a window.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

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

} // namespace oxygen::examples::ui

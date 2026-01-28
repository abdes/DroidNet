//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/Settings/SettingsService.h"

namespace ImGui {
class FileBrowser;
} // namespace ImGui

namespace oxygen::examples {

//! Filter description for the ImGui file browser.
struct FileBrowserFilter {
  std::string description;
  std::vector<std::string> extensions;
};

//! Configuration for the ImGui file browser.
struct FileBrowserConfig {
  std::string title;
  std::filesystem::path initial_directory;
  std::vector<FileBrowserFilter> filters;
  bool select_directory { false };
  bool allow_create_directory { true };
  bool allow_multi_select { false };
};

//! ImGui file browser service with a simple, reusable API.
/*!
 Provides a lightweight wrapper over ImGuiFileBrowser that can be reused across
 panels and demos. Call `Open()` on demand, then call `UpdateAndDraw()` each
 frame. Consume the selection with `ConsumeSelection()`.
 */
class FileBrowserService {
public:
  FileBrowserService();
  ~FileBrowserService();

  FileBrowserService(const FileBrowserService&) = delete;
  auto operator=(const FileBrowserService&) -> FileBrowserService& = delete;
  FileBrowserService(FileBrowserService&&) = delete;
  auto operator=(FileBrowserService&&) -> FileBrowserService& = delete;

  //! Open the file browser with the given configuration.
  void Open(const FileBrowserConfig& config);

  //! Draw the file browser if open and capture selection.
  void UpdateAndDraw();

  //! Returns the selected path if available and clears it.
  auto ConsumeSelection() -> std::optional<std::filesystem::path>;

  //! Returns true if the browser window is currently open.
  [[nodiscard]] auto IsOpen() const noexcept -> bool;

  //! Overrides the settings key used to persist window size.
  void SetSettingsKey(std::string key);

private:
  auto BuildTypeFilters(const FileBrowserConfig& config)
    -> std::vector<std::string>;
  auto ResolveSettings() const noexcept
    -> oxygen::observer_ptr<SettingsService>;
  auto MakeSettingsKey(std::string_view title) const -> std::string;

  std::unique_ptr<ImGui::FileBrowser> browser_;
  std::optional<std::filesystem::path> selection_;
  std::string open_label_ {};
  std::string settings_key_override_ {};
  std::string settings_key_ {};
};

//! Creates a file browser configuration for PAK files.
auto MakePakFileBrowserConfig() -> FileBrowserConfig;

//! Creates a file browser configuration for FBX files.
auto MakeFbxFileBrowserConfig() -> FileBrowserConfig;

//! Creates a file browser configuration for FBX/GLTF/GLB files.
auto MakeModelFileBrowserConfig() -> FileBrowserConfig;

//! Creates a directory browser configuration for model source folders.
auto MakeModelDirectoryBrowserConfig() -> FileBrowserConfig;

//! Creates a file browser configuration for loose cooked index files.
auto MakeLooseCookedIndexBrowserConfig() -> FileBrowserConfig;

//! Creates a file browser configuration for skybox images.
auto MakeSkyboxFileBrowserConfig() -> FileBrowserConfig;

} // namespace oxygen::examples

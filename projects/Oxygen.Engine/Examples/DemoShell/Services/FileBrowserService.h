//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/ObserverPtr.h>

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

//! Centralized content root configuration for demo assets.
/*!
 The examples project is read only: a demo's cooked content and ad-hoc imports
 live in folders the demo owns.
*/
struct ContentRootConfig {
  //! The examples project root; `Examples/Showcase` by default.
  std::filesystem::path content_root;
  //! The demo's cooked root to browse; none when empty.
  std::filesystem::path cooked_root;
  //! The folder of the demo's paks; none when empty.
  std::filesystem::path pak_directory;
  //! The demo's scratch project for ad-hoc model imports; imports are
  //! unavailable when empty.
  std::filesystem::path scratch_root;
};

//! Resolved content root paths and sub-categories.
struct ContentRootPaths {
  //! The examples project root.
  std::filesystem::path content_root;
  //! Raw DCC models, `Content/SourceMedia/DCC`.
  std::filesystem::path dcc_directory;
  //! Raw images, `Content/SourceMedia/Images`.
  std::filesystem::path images_directory;
  //! The demo's paks.
  std::filesystem::path pak_directory;
  std::filesystem::path cooked_root;
  //! The demo's transient project of ad-hoc model imports: its own `Scratch`
  //! mount, which holds their records, and `.cooked/`, away from the examples
  //! project.
  std::filesystem::path scratch_root;
};

//! ImGui file browser service with a simple, reusable API.
/*!
 Provides a lightweight wrapper over ImGuiFileBrowser that can be reused across
 panels and demos. Call `Open()` on demand, then call `UpdateAndDraw()` each
 frame. Consume the result with `ConsumeResult()`.
 */
class FileBrowserService {
public:
  using RequestId = std::uint64_t;

  FileBrowserService();
  ~FileBrowserService();

  FileBrowserService(const FileBrowserService&) = delete;
  auto operator=(const FileBrowserService&) -> FileBrowserService& = delete;
  FileBrowserService(FileBrowserService&&) = delete;
  auto operator=(FileBrowserService&&) -> FileBrowserService& = delete;

  //! Open the file browser with the given configuration.
  auto Open(const FileBrowserConfig& config) -> RequestId;

  //! Draw the file browser if open and capture selection.
  void UpdateAndDraw();

  //! Indicates the type of file browser result.
  enum class ResultKind {
    kSelected,
    kCanceled,
  };

  //! File browser outcome.
  struct Result {
    ResultKind kind { ResultKind::kSelected };
    std::filesystem::path path;
    RequestId request_id { 0 };
  };

  //! Returns the latest result if available and clears it.
  auto ConsumeResult(RequestId request_id) -> std::optional<Result>;

  //! Returns true if the browser window is currently open.
  [[nodiscard]] auto IsOpen() const noexcept -> bool;

  //! Overrides the settings key used to persist window size.
  void SetSettingsKey(std::string key);

  //! Configure shared content roots used by file browsers and panels.
  void ConfigureContentRoots(const ContentRootConfig& config);

  //! Returns the resolved content root paths.
  [[nodiscard]] auto GetContentRoots() const -> ContentRootPaths;

private:
  auto BuildTypeFilters(const FileBrowserConfig& config)
    -> std::vector<std::string>;
  [[nodiscard]] auto ResolveSettings() const noexcept
    -> observer_ptr<SettingsService>;
  [[nodiscard]] auto MakeSettingsKey(std::string_view title) const
    -> std::string;

  std::unique_ptr<ImGui::FileBrowser> browser_;
  std::optional<Result> result_;
  bool was_open_ { false };
  int last_update_frame_ { -1 };
  RequestId next_request_id_ { 0 };
  RequestId active_request_id_ { 0 };
  std::string open_label_;
  std::string settings_key_override_;
  std::string settings_key_;
  std::optional<ContentRootConfig> content_root_config_;
};

//! Creates a file browser configuration for PAK files.
auto MakePakFileBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig;

//! Picks a loose cooked root folder.
auto MakeCookedRootBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig;

//! Picks a folder of source models.
auto MakeSourceFolderBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig;

//! Picks a model file to import, of the model file `extensions`.
auto MakeModelFileBrowserConfig(const ContentRootPaths& roots,
  std::vector<std::string> extensions) -> FileBrowserConfig;

//! Creates a file browser configuration for skybox images.
auto MakeSkyboxFileBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig;

} // namespace oxygen::examples

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include <Oxygen/Base/Logging.h>

#include "DemoShell/FileBrowser/imfilebrowser.h"
#include "DemoShell/Services/FileBrowserService.h"

namespace oxygen::examples {

namespace {

  auto ResolveDefaultContentRoot() -> std::filesystem::path
  {
    const auto source_path
      = std::filesystem::path(std::source_location::current().file_name());
    return source_path.parent_path().parent_path().parent_path() / "Content";
  }

  auto ResolveContentRoots(const ContentRootConfig& config) -> ContentRootPaths
  {
    const auto content_root = config.content_root.empty()
      ? ResolveDefaultContentRoot()
      : config.content_root;
    const auto cooked_root = config.cooked_root.empty()
      ? content_root / ".cooked"
      : config.cooked_root;

    return ContentRootPaths {
      .content_root = content_root,
      .fbx_directory = content_root / "fbx",
      .glb_directory = content_root / "glb",
      .gltf_directory = content_root / "gltf",
      .textures_directory = content_root / "textures",
      .images_directory = content_root / "images",
      .pak_directory = content_root / "pak",
      .cooked_root = cooked_root,
    };
  }

  auto FlattenExtensions(const FileBrowserConfig& config)
    -> std::vector<std::string>
  {
    std::vector<std::string> extensions;
    for (const auto& filter : config.filters) {
      for (const auto& ext : filter.extensions) {
        if (ext.empty()) {
          continue;
        }
        extensions.push_back(ext);
      }
    }
    if (extensions.empty()) {
      extensions.emplace_back(".*");
    }
    std::ranges::sort(extensions);
    extensions.erase(std::ranges::unique(extensions).begin(), extensions.end());
    return extensions;
  }

  auto MakeFilter(std::string description, std::vector<std::string> extensions)
    -> FileBrowserFilter
  {
    return FileBrowserFilter {
      .description = std::move(description),
      .extensions = std::move(extensions),
    };
  }

} // namespace

FileBrowserService::FileBrowserService()
  : browser_(
      std::make_unique<ImGui::FileBrowser>(ImGuiFileBrowserFlags_CloseOnEsc))
{
}

FileBrowserService::~FileBrowserService() = default;

auto FileBrowserService::Open(const FileBrowserConfig& config) -> RequestId
{
  ImGuiFileBrowserFlags flags
    = ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_ConfirmOnEnter;
  if (config.select_directory) {
    flags |= ImGuiFileBrowserFlags_SelectDirectory
      | ImGuiFileBrowserFlags_HideRegularFiles;
  }
  if (config.allow_create_directory) {
    flags |= ImGuiFileBrowserFlags_CreateNewDir;
  }
  if (config.allow_multi_select) {
    flags |= ImGuiFileBrowserFlags_MultipleSelection;
  }

  const auto base_directory
    = config.initial_directory.empty() ? std::filesystem::current_path() : [&] {
        std::error_code ec;
        if (std::filesystem::exists(config.initial_directory, ec)
          && std::filesystem::is_directory(config.initial_directory, ec)) {
          return config.initial_directory;
        }
        return std::filesystem::current_path();
      }();
  const std::string title
    = config.title.empty() ? "file browser" : config.title;

  flags |= ImGuiFileBrowserFlags_SkipItemsCausingError;
  browser_ = std::make_unique<ImGui::FileBrowser>(flags, base_directory);
  browser_->SetTitle(title);

  open_label_ = title + "##filebrowser_"
    + std::to_string(reinterpret_cast<size_t>(browser_.get()));

  const auto settings = ResolveSettings();
  settings_key_ = settings_key_override_.empty() ? MakeSettingsKey(title)
                                                 : settings_key_override_;
  if (settings) {
    if (const auto size = settings->GetVec2i(settings_key_ + ".window_size")) {
      browser_->SetWindowSize(size->first, size->second);
    }
  }

  const auto type_filters = BuildTypeFilters(config);
  if (!type_filters.empty()) {
    browser_->SetTypeFilters(type_filters);
  }

  browser_->Open();
  result_.reset();
  active_request_id_ = ++next_request_id_;
  return active_request_id_;
}

void FileBrowserService::UpdateAndDraw()
{
  if (!browser_) {
    return;
  }

  const int frame = ImGui::GetFrameCount();
  if (frame == last_update_frame_) {
    return;
  }
  last_update_frame_ = frame;

  browser_->Display();
  const bool is_open = browser_->IsOpened();
  if (is_open != was_open_) {
    LOG_F(INFO, "FileBrowserService: Open state changed (open={})", is_open);
  }
  if (is_open && !open_label_.empty()) {
    if (const ImGuiWindow* window
      = ImGui::FindWindowByName(open_label_.c_str())) {
      const auto settings = ResolveSettings();
      if (settings) {
        const int width = static_cast<int>(window->Size.x);
        const int height = static_cast<int>(window->Size.y);
        settings->SetVec2i(settings_key_ + ".window_size", { width, height });
      }
    }
  }
  const bool has_selection = browser_->HasSelected();
  if (has_selection) {
    LOG_F(INFO, "FileBrowserService: Selection confirmed");
    result_ = Result {
      .kind = ResultKind::kSelected,
      .path = browser_->GetSelected(),
      .request_id = active_request_id_,
    };
    browser_->ClearSelected();
    browser_->Close();
  } else if (was_open_ && !is_open && !result_) {
    LOG_F(INFO, "FileBrowserService: Closed without selection");
    result_ = Result { .kind = ResultKind::kCanceled,
      .path = {},
      .request_id = active_request_id_ };
  }

  was_open_ = is_open;
}

auto FileBrowserService::ConsumeResult(const RequestId request_id)
  -> std::optional<Result>
{
  if (!result_ || result_->request_id != request_id) {
    return std::nullopt;
  }
  return std::exchange(result_, std::nullopt);
}

auto FileBrowserService::IsOpen() const noexcept -> bool
{
  return browser_ && browser_->IsOpened();
}

void FileBrowserService::SetSettingsKey(std::string key)
{
  settings_key_override_ = std::move(key);
}

void FileBrowserService::ConfigureContentRoots(const ContentRootConfig& config)
{
  content_root_config_ = config;
}

auto FileBrowserService::GetContentRoots() const -> ContentRootPaths
{
  return ResolveContentRoots(
    content_root_config_.value_or(ContentRootConfig {}));
}

auto FileBrowserService::BuildTypeFilters(const FileBrowserConfig& config)
  -> std::vector<std::string>
{
  return FlattenExtensions(config);
}

auto FileBrowserService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::ForDemoApp();
}

auto FileBrowserService::MakeSettingsKey(std::string_view title) const
  -> std::string
{
  std::string key;
  key.reserve(title.size());
  for (const char ch : title) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
      || (ch >= '0' && ch <= '9')) {
      key.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    } else {
      key.push_back('_');
    }
  }
  return "file_browser." + key;
}

auto MakePakFileBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig
{
  return FileBrowserConfig {
    .title = "Select PAK File",
    .initial_directory = roots.pak_directory,
    .filters = { MakeFilter("PAK", { ".pak" }) },
  };
}

auto MakeFbxFileBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig
{
  return FileBrowserConfig {
    .title = "Select FBX File",
    .initial_directory = roots.fbx_directory,
    .filters = { MakeFilter("FBX", { ".fbx" }) },
  };
}

auto MakeModelFileBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig
{
  return FileBrowserConfig {
    .title = "Select Model File",
    .initial_directory = roots.fbx_directory,
    .filters = { MakeFilter("Model", { ".fbx", ".gltf", ".glb" }) },
  };
}

auto MakeModelDirectoryBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig
{
  return FileBrowserConfig {
    .title = "Select Model Directory",
    .initial_directory = roots.content_root,
    .select_directory = true,
    .allow_create_directory = false,
  };
}

auto MakeLooseCookedIndexBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig
{
  return FileBrowserConfig {
    .title = "Select Loose Cooked Index",
    .initial_directory = roots.cooked_root,
    .filters = { MakeFilter("Index", { ".bin" }) },
  };
}

auto MakeSkyboxFileBrowserConfig(const ContentRootPaths& roots)
  -> FileBrowserConfig
{
  return FileBrowserConfig {
    .title = "Select Skybox Image",
    .initial_directory = roots.images_directory,
    .filters = { MakeFilter(
      "Skybox", { ".hdr", ".exr", ".png", ".jpg", ".jpeg", ".tga", ".bmp" }) },
  };
}

} // namespace oxygen::examples

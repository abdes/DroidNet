//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "FilePicker.h"

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/StringUtils.h>

#if defined(OXYGEN_WINDOWS)
#  include <shobjidl_core.h>
#  include <windows.h>
#  include <wrl/client.h>
#endif

namespace oxygen::examples::render_scene::ui {

#if defined(OXYGEN_WINDOWS)

namespace {

  //! RAII wrapper for COM initialization
  class ScopedCoInitialize {
  public:
    ScopedCoInitialize()
    {
      const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      initialized_ = SUCCEEDED(hr);
      // COM already initialized in different mode, proceed without ownership
      if (hr == RPC_E_CHANGED_MODE) {
        initialized_ = false;
      }
    }

    ~ScopedCoInitialize()
    {
      if (initialized_) {
        CoUninitialize();
      }
    }

    ScopedCoInitialize(const ScopedCoInitialize&) = delete;
    auto operator=(const ScopedCoInitialize&) -> ScopedCoInitialize& = delete;
    ScopedCoInitialize(ScopedCoInitialize&&) = delete;
    auto operator=(ScopedCoInitialize&&) -> ScopedCoInitialize& = delete;

  private:
    bool initialized_ { false };
  };

} // namespace

#endif

auto ShowFilePicker(const FilePickerConfig& config)
  -> std::optional<std::filesystem::path>
{
#if defined(OXYGEN_WINDOWS)
  ScopedCoInitialize com;

  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  const HRESULT hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));

  if (FAILED(hr) || !dialog) {
    return std::nullopt;
  }

  // Configure file filters
  if (!config.filters.empty()) {
    std::vector<COMDLG_FILTERSPEC> filter_specs;
    filter_specs.reserve(config.filters.size());

    for (const auto& filter : config.filters) {
      filter_specs.push_back(
        { filter.description.c_str(), filter.pattern.c_str() });
    }

    (void)dialog->SetFileTypes(
      static_cast<UINT>(filter_specs.size()), filter_specs.data());
  }

  // Set default extension
  if (!config.default_extension.empty()) {
    (void)dialog->SetDefaultExtension(config.default_extension.c_str());
  }

  // Set dialog title
  if (!config.title.empty()) {
    (void)dialog->SetTitle(config.title.c_str());
  }

  // Set initial directory
  if (!config.initial_directory.empty()) {
    Microsoft::WRL::ComPtr<IShellItem> folder_item;
    const HRESULT folder_hr = SHCreateItemFromParsingName(
      config.initial_directory.c_str(), nullptr, IID_PPV_ARGS(&folder_item));

    if (SUCCEEDED(folder_hr) && folder_item) {
      (void)dialog->SetFolder(folder_item.Get());
    }
  }

  // Show dialog
  const HRESULT show_hr = dialog->Show(nullptr);
  if (FAILED(show_hr)) {
    return std::nullopt;
  }

  // Get selected file
  Microsoft::WRL::ComPtr<IShellItem> result_item;
  if (FAILED(dialog->GetResult(&result_item)) || !result_item) {
    return std::nullopt;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr
    = result_item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);

  if (FAILED(name_hr) || !wide_path) {
    return std::nullopt;
  }

  std::string utf8_path;
  oxygen::string_utils::WideToUtf8(wide_path, utf8_path);
  CoTaskMemFree(wide_path);

  if (utf8_path.empty()) {
    return std::nullopt;
  }

  return std::filesystem::path(utf8_path);
#else
  // Platform not supported
  return std::nullopt;
#endif
}

/*!
 Displays a platform-native directory picker dialog.

 @param config Dialog configuration including title and initial folder
 @return Selected directory path, or std::nullopt if canceled

 ### Platform Support

 - **Windows:** Uses IFileOpenDialog with folder selection
 - **macOS/Linux:** Currently not implemented (returns std::nullopt)

 ### Usage Examples

 ```cpp
 DirectoryPickerConfig config;
 config.title = L"Select Model Folder";

 if (const auto path = ShowDirectoryPicker(config)) {
   // User selected a directory
   ScanAssets(*path);
 }
 ```

 @warning This function initializes COM and may affect application state
 @see DirectoryPickerConfig
 */
auto ShowDirectoryPicker(const DirectoryPickerConfig& config)
  -> std::optional<std::filesystem::path>
{
#if defined(OXYGEN_WINDOWS)
  ScopedCoInitialize com;

  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  const HRESULT hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));

  if (FAILED(hr) || !dialog) {
    return std::nullopt;
  }

  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    options |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    (void)dialog->SetOptions(options);
  }

  if (!config.title.empty()) {
    (void)dialog->SetTitle(config.title.c_str());
  }

  if (!config.initial_directory.empty()) {
    Microsoft::WRL::ComPtr<IShellItem> folder_item;
    const HRESULT folder_hr = SHCreateItemFromParsingName(
      config.initial_directory.c_str(), nullptr, IID_PPV_ARGS(&folder_item));

    if (SUCCEEDED(folder_hr) && folder_item) {
      (void)dialog->SetFolder(folder_item.Get());
    }
  }

  const HRESULT show_hr = dialog->Show(nullptr);
  if (FAILED(show_hr)) {
    return std::nullopt;
  }

  Microsoft::WRL::ComPtr<IShellItem> result_item;
  if (FAILED(dialog->GetResult(&result_item)) || !result_item) {
    return std::nullopt;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr
    = result_item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);

  if (FAILED(name_hr) || !wide_path) {
    return std::nullopt;
  }

  std::string utf8_path;
  oxygen::string_utils::WideToUtf8(wide_path, utf8_path);
  CoTaskMemFree(wide_path);

  if (utf8_path.empty()) {
    return std::nullopt;
  }

  return std::filesystem::path(utf8_path);
#else
  return std::nullopt;
#endif
}

auto MakePakFilePickerConfig() -> FilePickerConfig
{
  FilePickerConfig config;
  config.filters = {
    { L"Oxygen PAK files (*.pak)", L"*.pak" },
    { L"All files (*.*)", L"*.*" },
  };
  config.default_extension = L"pak";
  config.title = L"Select PAK File";
  return config;
}

auto MakeFbxFilePickerConfig() -> FilePickerConfig
{
  FilePickerConfig config;
  config.filters = {
    { L"FBX files (*.fbx)", L"*.fbx" },
    { L"All files (*.*)", L"*.*" },
  };
  config.default_extension = L"fbx";
  config.title = L"Select FBX File";
  return config;
}

auto MakeModelFilePickerConfig() -> FilePickerConfig
{
  FilePickerConfig config;
  config.filters = {
    { L"FBX/GLTF/GLB files (*.fbx;*.gltf;*.glb)", L"*.fbx;*.gltf;*.glb" },
    { L"FBX files (*.fbx)", L"*.fbx" },
    { L"glTF files (*.gltf)", L"*.gltf" },
    { L"GLB files (*.glb)", L"*.glb" },
    { L"All files (*.*)", L"*.*" },
  };
  config.default_extension = L"gltf";
  config.title = L"Select 3D Model File";
  return config;
}

/*!
 Creates a directory picker configuration for model source folders.

 @return Pre-configured DirectoryPickerConfig for selecting model directories

 ### Usage Examples

 ```cpp
 if (const auto path = ShowDirectoryPicker(MakeModelDirectoryPickerConfig())) {
   ScanModelDirectory(*path);
 }
 ```
 */
auto MakeModelDirectoryPickerConfig() -> DirectoryPickerConfig
{
  DirectoryPickerConfig config;
  config.title = L"Select Model Directory";
  return config;
}

auto MakeLooseCookedIndexPickerConfig() -> FilePickerConfig
{
  FilePickerConfig config;
  config.filters = {
    { L"Loose cooked index (container.index.bin)", L"container.index.bin" },
    { L"Binary files (*.bin)", L"*.bin" },
    { L"All files (*.*)", L"*.*" },
  };
  config.default_extension = L"bin";
  config.title = L"Select Loose Cooked Index";
  return config;
}

} // namespace oxygen::examples::render_scene::ui

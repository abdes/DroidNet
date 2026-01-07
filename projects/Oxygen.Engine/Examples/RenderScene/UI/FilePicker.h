//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace oxygen::examples::render_scene::ui {

//! File filter specification for file picker dialogs
struct FileFilter {
  std::wstring description;
  std::wstring pattern;
};

//! Configuration for file picker dialog
struct FilePickerConfig {
  std::vector<FileFilter> filters;
  std::wstring default_extension;
  std::wstring title;
  std::filesystem::path initial_directory;
};

/*!
 Displays a platform-native file picker dialog.

 @param config Dialog configuration including filters and default settings
 @return Selected file path, or std::nullopt if canceled

 ### Platform Support

 - **Windows:** Uses IFileOpenDialog COM interface
 - **macOS/Linux:** Currently not implemented (returns std::nullopt)

 ### Usage Examples

 ```cpp
 FilePickerConfig config;
 config.filters = {
   {L"PAK files (*.pak)", L"*.pak"},
   {L"All files (*.*)", L"*.*"}
 };
 config.default_extension = L"pak";
 config.title = L"Select PAK File";

 if (const auto path = ShowFilePicker(config)) {
   // User selected a file
   LoadPakFile(*path);
 }
 ```

 @warning This function initializes COM and may affect application state
 @see FilePickerConfig, FileFilter
 */
auto ShowFilePicker(const FilePickerConfig& config)
  -> std::optional<std::filesystem::path>;

/*!
 Creates a file picker configuration for PAK files.

 @return Pre-configured FilePickerConfig for selecting .pak files

 ### Usage Examples

 ```cpp
 if (const auto path = ShowFilePicker(MakePakFilePickerConfig())) {
   MountPakFile(*path);
 }
 ```
 */
auto MakePakFilePickerConfig() -> FilePickerConfig;

/*!
 Creates a file picker configuration for FBX files.

 @return Pre-configured FilePickerConfig for selecting .fbx files

 ### Usage Examples

 ```cpp
 if (const auto path = ShowFilePicker(MakeFbxFilePickerConfig())) {
   ImportFbxFile(*path);
 }
 ```
 */
auto MakeFbxFilePickerConfig() -> FilePickerConfig;

/*!
 Creates a file picker configuration for loose cooked index files.

 @return Pre-configured FilePickerConfig for selecting container.index.bin files

 ### Usage Examples

 ```cpp
 if (const auto path = ShowFilePicker(MakeLooseCookedIndexPickerConfig())) {
   LoadLooseCookedIndex(*path);
 }
 ```
 */
auto MakeLooseCookedIndexPickerConfig() -> FilePickerConfig;

} // namespace oxygen::examples::render_scene::ui

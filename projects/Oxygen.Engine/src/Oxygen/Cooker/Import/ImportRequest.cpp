//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>

namespace oxygen::content::import {

//! Stable command-line spelling of the import format.
[[nodiscard]] inline auto to_string(ImportFormat format) -> std::string_view
{
  switch (format) {
  case ImportFormat::kFbx:
    return "fbx";
  case ImportFormat::kGltf:
    return "gltf";
  case ImportFormat::kTextureImage:
    return "texture";
  case ImportFormat::kUnknown:
    return "unknown";
  }
  return "unknown";
}

auto ImportRequest::GetSceneName() const -> std::string
{
  const auto leaf = std::string_view(asset_virtual_path)
                      .substr(asset_virtual_path.rfind('/') + 1);
  const auto dot = leaf.rfind('.');
  return std::string(
    dot == std::string_view::npos || dot == 0 ? leaf : leaf.substr(0, dot));
}

auto ImportRequest::ModelProductVirtualPath(const std::string_view name,
  const std::string_view extension) const -> std::string
{
  auto path = asset_virtual_path;
  path.push_back('/');
  path.append(name);
  path.append(extension);
  return path;
}

auto ImportRequest::GetTextureDescriptorRelPath() const -> std::string
{
  auto relative = std::string {};
  if (!internal::IsCanonicalVirtualPath(asset_virtual_path)
    || !internal::TryVirtualPathToRelPath(asset_virtual_path, relative)
    || std::filesystem::path(relative).extension() != ".otex") {
    throw std::invalid_argument(
      "Texture virtual_path must name an .otex descriptor within its mount: "
      + asset_virtual_path);
  }
  return relative;
}

auto ImportRequest::OutputIdentity() const -> std::string_view
{
  if (!asset_virtual_path.empty()) {
    return asset_virtual_path;
  }
  if (options.scripting.import_kind == ScriptingImportKind::kScriptingSidecar) {
    return options.scripting.target_scene_virtual_path;
  }
  if (physics.has_value()) {
    return physics->target_scene_virtual_path;
  }
  if (buffer_container.has_value()) {
    return BufferContainerIdentity({ .source_mount_path = source_mount_path });
  }
  return {};
}

auto BufferContainerIdentity(const BufferContainerPaths& paths)
  -> std::string_view
{
  if (!paths.virtual_path.empty()) {
    return paths.virtual_path;
  }
  constexpr auto kAuthoringSuffix = std::string_view { ".json" };
  return paths.source_mount_path.ends_with(kAuthoringSuffix)
    ? paths.source_mount_path.substr(
        0, paths.source_mount_path.size() - kAuthoringSuffix.size())
    : paths.source_mount_path;
}

auto ImportRequest::OutputMountRoot() const -> std::string_view
{
  return content::MountRootOf(OutputIdentity());
}

auto ImportRequest::ResolveCookedRoot() const -> std::filesystem::path
{
  if (cooked_root.has_value()) {
    return cooked_root->lexically_normal();
  }
  const auto leaf = std::filesystem::path(OutputMountRoot()).filename();
  std::filesystem::path base;
  if (!source_path.empty()) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(source_path, error);
    if (!error) {
      base = absolute.parent_path();
    }
  }
  if (base.empty()) {
    base = std::filesystem::temp_directory_path();
  }
  return leaf.empty() || base.filename() == leaf ? base : base / leaf;
}

namespace {
  //! Lower-case an ASCII string in-place and return it.
  auto ToLowerAscii(std::string value) -> std::string
  {
    std::ranges::transform(
      value, value.begin(), [](const unsigned char ch) -> char {
        return static_cast<char>(std::tolower(ch));
      });
    return value;
  }
} // namespace

//! Auto-detects the import format from the source path extension.
auto ImportRequest::GetFormat() const -> ImportFormat
{
  const auto ext = ToLowerAscii(source_path.extension().string());

  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga"
    || ext == ".bmp" || ext == ".psd" || ext == ".gif" || ext == ".hdr"
    || ext == ".pic" || ext == ".ppm" || ext == ".pgm" || ext == ".pnm"
    || ext == ".exr" || ext == ".webp" || ext == ".ktx2") {
    return ImportFormat::kTextureImage;
  }
  if (ext == ".gltf" || ext == ".glb") {
    return ImportFormat::kGltf;
  }
  if (ext == ".fbx") {
    return ImportFormat::kFbx;
  }

  return ImportFormat::kUnknown;
}

} // namespace oxygen::content::import

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
  const auto stem = source_path.stem().string();
  return stem.empty() ? "Scene" : stem;
}

auto ImportRequest::GetTextureDescriptorRelPath() const -> std::string
{
  if (!texture_virtual_path.empty()) {
    auto relative = std::string {};
    if (!internal::IsCanonicalVirtualPath(texture_virtual_path)
      || !internal::TryVirtualPathToRelPath(
        *this, texture_virtual_path, relative)
      || std::filesystem::path(relative).extension() != ".otex") {
      throw std::invalid_argument(
        "Texture virtual_path must name an .otex descriptor within its mount: "
        + texture_virtual_path);
    }
    return relative;
  }
  if (job_name.has_value() && !job_name->empty()) {
    return loose_cooked_layout.TextureDescriptorRelPath(*job_name, *job_name);
  }
  auto normalized = source_path.lexically_normal();
  normalized.make_preferred();
  auto identity = normalized.generic_string();
  auto name = source_path.stem().string();
  if (name.empty()) {
    name = identity;
  }
  if (identity.empty()) {
    identity = name;
  }
  return loose_cooked_layout.TextureDescriptorRelPath(name, identity);
}

auto ImportRequest::ResolveCookedRoot() const -> std::filesystem::path
{
  if (cooked_root.has_value()) {
    return cooked_root->lexically_normal();
  }
  auto leaf = std::filesystem::path(loose_cooked_layout.virtual_mount_root)
                .lexically_normal()
                .filename();
  if (leaf.empty()) {
    leaf = ".cooked";
  }
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
  return base.filename() == leaf ? base : base / leaf;
}

namespace {
  //! Lower-case an ASCII string in-place and return it.
  auto ToLowerAscii(std::string value) -> std::string
  {
    std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
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
    || ext == ".exr") {
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

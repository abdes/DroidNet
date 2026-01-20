//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include <Oxygen/Content/Import/ImportRequest.h>

namespace oxygen::content::import {

//! Upper-case style ToString for compatibility with project naming.
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

namespace {
  //! Lower-case an ASCII string in-place and return it.
  auto ToLowerAscii(std::string value) -> std::string
  {
    std::transform(
      value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
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

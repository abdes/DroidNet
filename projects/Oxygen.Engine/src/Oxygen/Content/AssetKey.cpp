//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/AssetKey.h>

auto oxygen::content::to_string(AssetType type) noexcept -> const char*
{
  switch (type) {
    // clang-format off
    case AssetType::kUnknown: return "Unknown";
    case AssetType::kGeometry: return "Geometry";
    case AssetType::kMesh: return "Mesh";
    case AssetType::kTexture: return "Texture";
    case AssetType::kShader: return "Shader";
    case AssetType::kMaterial: return "Material";
    case AssetType::kAudio: return "Audio";
    case AssetType::kMaxAssetType: return "__Max__";
    // clang-format on
  }
  return "__NotSupported__";
}

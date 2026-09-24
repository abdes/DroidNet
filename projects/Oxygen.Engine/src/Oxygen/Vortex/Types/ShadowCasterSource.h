//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/vec4.hpp>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

namespace oxygen::vortex {

//! CPU source dependency before GPU instancing combines separate casters.
struct ShadowCasterSource {
  DrawMetadata draw {};
  glm::vec4 bounds { 0.0F };
  scene::NodeHandle node;
  data::AssetKey geometry_asset_key;
  std::uint32_t lod_index { 0U };
  std::uint32_t geometry_generation { 0U };
  std::uint64_t geometry_content_revision { 0U };
  std::uint32_t material_generation { 0U };
};

struct ShadowCasterDependency {
  glm::vec4 bounds { 0.0F };
  std::uint64_t fingerprint { 0U };
  bool reusable { false };
};

} // namespace oxygen::vortex

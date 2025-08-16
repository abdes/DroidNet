//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
}

namespace oxygen::engine::sceneprep {

struct RenderItemData {
  std::uint32_t lod_index = 0;
  std::uint32_t submesh_index = 0;

  // Asset references (immutable, shareable)
  std::shared_ptr<const oxygen::data::GeometryAsset> geometry;
  std::shared_ptr<const oxygen::data::MaterialAsset> material;

  // Cached scene state
  glm::vec4 world_bounding_sphere { 0.0f, 0.0f, 0.0f, 0.0f };
  glm::mat4 world_transform { 1.0f };

  // Rendering flags
  bool cast_shadows = true;
  bool receive_shadows = true;

  // Optional future extensions
  // std::uint32_t render_layer = 0;
  // std::uint64_t instance_id = 0; // temporal tracking
};

} // namespace oxygen::engine::sceneprep

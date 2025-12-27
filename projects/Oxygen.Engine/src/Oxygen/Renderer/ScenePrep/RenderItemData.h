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
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
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
  // Renderer-facing material reference that carries source-aware texture keys
  // (opaque `content::ResourceKey`) alongside the material data.
  sceneprep::MaterialRef material;
  // Stable registry handle (preferred going forward). Populated during
  // emission; pointer retained temporarily for transition (will migrate
  // downstream users to handle-based access / bindless indirection).
  MaterialHandle material_handle { 0U };

  // Cached scene state
  glm::vec4 world_bounding_sphere { 0.0f, 0.0f, 0.0f, 0.0f };
  // Stable reference into TransformUploader
  TransformHandle transform_handle { 0U };

  // Rendering flags
  bool cast_shadows = true;
  bool receive_shadows = true;

  // Optional future extensions
  // std::uint32_t render_layer = 0;
  // std::uint64_t instance_id = 0; // temporal tracking
};

} // namespace oxygen::engine::sceneprep

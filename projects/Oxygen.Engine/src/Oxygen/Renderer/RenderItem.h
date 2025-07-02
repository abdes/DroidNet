//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::data {
class Mesh;
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::engine {

//! Immutable, data-driven snapshot of a renderable entity for the renderer.
/*!
 RenderItem is a self-sufficient, immutable struct containing all data required
 for rendering a single item in the scene. It is constructed from the scene
 system after culling, and contains no references to SceneNode or mutable scene
 data. All members are public for data-driven access; only minimal helpers are
 provided.

 - Mesh/material pointers reference shared, immutable assets.
 - World transform is cached at construction.
 - Only rendering-relevant flags are snapshotted.
 - No mutability or encapsulation beyond construction-time helpers.

 @see Step 1 of the RenderItem migration plan.
*/
struct RenderItem {
  // === Geometry Data ===
  std::shared_ptr<const data::Mesh> mesh;

  // === Material and Shading ===
  std::shared_ptr<const data::MaterialAsset> material;

  // === Transformation ===
  //! Object to world transformation
  glm::mat4 world_transform { 1.0f };
  //! Inverse transpose of world (for normals)
  glm::mat4 normal_transform { 1.0f };

  // === Snapshotted SceneNode Flags (REQUIRED for rendering) ===
  bool cast_shadows = true;
  bool receive_shadows = true;

  // === Optional Render State ===
  uint32_t render_layer = 0; // e.g., for pass selection or sorting
  uint32_t render_flags = 0; // bitmask for custom per-item state

  // === Culling Data (world-space only) ===
  static constexpr glm::vec4 kDefaultBoundingSphere { 0.0f, 0.0f, 0.0f, 0.0f };
  glm::vec4 bounding_sphere { kDefaultBoundingSphere };
  glm::vec3 bounding_box_min { 0.0f, 0.0f, 0.0f };
  glm::vec3 bounding_box_max { 0.0f, 0.0f, 0.0f };

  // === Utility Methods ===
  //! Update only the world-space properties (bounding volumes, normal
  //! transform)
  OXGN_RNDR_API auto UpdatedTransformedProperties() -> void;

  //! Update all computed properties (including world-space and any other
  //! derived data)
  OXGN_RNDR_API auto UpdateComputedProperties() -> void;
};

} // namespace oxygen::engine

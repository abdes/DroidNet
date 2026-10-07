//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <optional>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace oxygen::interop::module::viewport {

  //! Position and orientation of a camera, in world or parent space.
  struct CameraPlacement {
    glm::vec3 position { 0.0F };
    glm::quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  };

  //! World frame of a node's parent; empty members mean a scene root.
  struct ParentFrame {
    std::optional<glm::mat4> world_matrix;
    std::optional<glm::quat> world_rotation;
  };

  //! Places a parent-space pose in the world.
  [[nodiscard]] inline auto ToWorldPlacement(const CameraPlacement& local,
    const ParentFrame& parent) noexcept -> CameraPlacement {
    CameraPlacement world = local;
    if (parent.world_matrix.has_value()) {
      world.position
        = glm::vec3(*parent.world_matrix * glm::vec4(local.position, 1.0F));
    }
    if (parent.world_rotation.has_value()) {
      world.rotation = *parent.world_rotation * local.rotation;
    }
    return world;
  }

  //! Expresses a world pose in the parent's space, with a unit rotation.
  [[nodiscard]] inline auto ToParentPlacement(const CameraPlacement& world,
    const ParentFrame& parent) noexcept -> CameraPlacement {
    CameraPlacement local = world;
    if (parent.world_matrix.has_value()) {
      local.position = glm::vec3(
        glm::inverse(*parent.world_matrix) * glm::vec4(world.position, 1.0F));
    }
    if (parent.world_rotation.has_value()) {
      local.rotation = glm::inverse(*parent.world_rotation) * world.rotation;
    }
    local.rotation = glm::normalize(local.rotation);
    return local;
  }

  //! Whether two poses match within authoring precision; `q` and `-q` are the
  //! same orientation.
  [[nodiscard]] inline auto IsSamePlacement(
    const CameraPlacement& a, const CameraPlacement& b) noexcept -> bool {
    constexpr float kPositionTolerance = 1.0e-4F;
    constexpr float kRotationDotTolerance = 1.0e-6F;
    return glm::distance(a.position, b.position) <= kPositionTolerance
      && std::abs(glm::dot(a.rotation, b.rotation))
      >= 1.0F - kRotationDotTolerance;
  }

} // namespace oxygen::interop::module::viewport

#pragma managed(pop)

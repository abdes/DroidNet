//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <optional>
#include <span>

#include <glm/vec3.hpp>

#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::scene {
  class Scene;
} // namespace oxygen::scene

namespace oxygen::interop::module {

  //! World bounds a view frames.
  struct FrameSphere {
    glm::vec3 center { 0.0F };
    float radius { 0.0F };
  };

  //! Extent given to a framed node without geometry: a light, a camera or an
  //! empty node.
  inline constexpr float kFramePointRadius = 0.5F;
  //! Extent framed around the origin when a scene has no nodes.
  inline constexpr float kEmptySceneFrameRadius = 5.0F;

  //! Bounds that frame `nodes` and their descendants' geometry.
  /*!
   Geometry contributes its world bounding sphere. A listed node without
   geometry anywhere in its subtree contributes its world position with
   `kFramePointRadius`. Missing nodes are skipped; no value when none of the
   nodes exist or the bounds are not finite.
  */
  [[nodiscard]] auto ResolveNodesFrameSphere(scene::Scene& scene,
    std::span<const scene::NodeHandle> nodes) -> std::optional<FrameSphere>;

  //! Bounds that frame the whole scene: its geometry, or every node's
  //! position when it has none, or the origin when it is empty. No value when
  //! the bounds are not finite.
  [[nodiscard]] auto ResolveSceneFrameSphere(scene::Scene& scene)
    -> std::optional<FrameSphere>;

} // namespace oxygen::interop::module

#pragma managed(pop)

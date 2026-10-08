//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <cmath>
#include <limits>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "EditorModule/ViewFraming.h"

namespace oxygen::interop::module {

  namespace {

    //! An axis-aligned box grown by spheres.
    class FrameBox {
    public:
      void Add(const glm::vec3& center, const float radius) {
        min_ = glm::min(min_, center - glm::vec3 { radius });
        max_ = glm::max(max_, center + glm::vec3 { radius });
        empty_ = false;
      }

      [[nodiscard]] auto IsEmpty() const noexcept -> bool { return empty_; }

      [[nodiscard]] auto ToSphere() const -> std::optional<FrameSphere> {
        if (empty_) {
          return std::nullopt;
        }
        const glm::vec3 center = 0.5F * (min_ + max_);
        const float radius = 0.5F * glm::length(max_ - min_);
        if (!std::isfinite(center.x) || !std::isfinite(center.y)
          || !std::isfinite(center.z) || !std::isfinite(radius)) {
          return std::nullopt;
        }
        return FrameSphere { .center = center, .radius = radius };
      }

    private:
      glm::vec3 min_ { std::numeric_limits<float>::max() };
      glm::vec3 max_ { std::numeric_limits<float>::lowest() };
      bool empty_ { true };
    };

    [[nodiscard]] auto WorldPosition(scene::SceneNode& node) -> glm::vec3 {
      return node.GetTransform().GetWorldPosition().value_or(glm::vec3 { 0.0F });
    }

    //! The world sphere of a node's geometry. A renderable reports (0,0,0,0)
    //! while its bounds are unavailable (unresolved LOD, no declared bounds);
    //! that is not geometry at the origin, so the node's own position stands
    //! in for it with the default extent.
    [[nodiscard]] auto GeometrySphere(scene::SceneNode& node)
      -> std::optional<FrameSphere> {
      auto renderable = node.GetRenderable();
      if (!renderable.HasGeometry()) {
        return std::nullopt;
      }
      const auto sphere = renderable.GetWorldBoundingSphere();
      if (!std::isfinite(sphere.x) || !std::isfinite(sphere.y)
        || !std::isfinite(sphere.z) || !std::isfinite(sphere.w)
        || sphere.w <= 0.0F) {
        return FrameSphere {
          .center = WorldPosition(node),
          .radius = kFramePointRadius,
        };
      }
      return FrameSphere {
        .center = { sphere.x, sphere.y, sphere.z },
        .radius = sphere.w,
      };
    }

    //! Adds the geometry of `node` and its descendants; true when any.
    auto AddSubtreeGeometry(scene::SceneNode node, FrameBox& box) -> bool {
      bool added = false;
      if (const auto sphere = GeometrySphere(node)) {
        box.Add(sphere->center, sphere->radius);
        added = true;
      }
      for (auto child = node.GetFirstChild(); child.has_value();
        child = child->GetNextSibling()) {
        added = AddSubtreeGeometry(*child, box) || added;
      }
      return added;
    }

  } // namespace

  auto ResolveNodesFrameSphere(scene::Scene& scene,
    const std::span<const scene::NodeHandle> nodes)
    -> std::optional<FrameSphere> {
    // Current world transforms and bounds, including this frame's edits.
    scene.Update();
    FrameBox box;
    for (const auto& handle : nodes) {
      auto node = scene.GetNode(handle);
      if (!node.has_value() || !node->IsAlive()) {
        continue;
      }
      if (!AddSubtreeGeometry(*node, box)) {
        box.Add(WorldPosition(*node), kFramePointRadius);
      }
    }
    return box.ToSphere();
  }

  auto ResolveSceneFrameSphere(scene::Scene& scene)
    -> std::optional<FrameSphere> {
    scene.Update();
    const auto roots = scene.GetRootNodes();
    if (roots.empty()) {
      return FrameSphere {
        .center = glm::vec3 { 0.0F },
        .radius = kEmptySceneFrameRadius,
      };
    }
    FrameBox geometry;
    for (const auto& root : roots) {
      (void)AddSubtreeGeometry(root, geometry);
    }
    if (!geometry.IsEmpty()) {
      return geometry.ToSphere();
    }

    // Only lights, cameras and empty nodes: frame where they are.
    FrameBox points;
    std::vector<scene::SceneNode> pending(roots.begin(), roots.end());
    while (!pending.empty()) {
      auto node = pending.back();
      pending.pop_back();
      points.Add(WorldPosition(node), kFramePointRadius);
      for (auto child = node.GetFirstChild(); child.has_value();
        child = child->GetNextSibling()) {
        pending.push_back(*child);
      }
    }
    return points.ToSphere();
  }

} // namespace oxygen::interop::module

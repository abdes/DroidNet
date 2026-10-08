//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace oxygen::vortex {

//! One corner of an overlay triangle, in world space, with straight alpha.
struct ViewOverlayVertex {
  glm::vec3 position { 0.0F };
  float pad0 { 0.0F };
  glm::vec4 color { 1.0F };
};
static_assert(sizeof(ViewOverlayVertex) == 32U);

//! A world-space segment drawn with a constant width in pixels.
struct ViewOverlayLine {
  glm::vec3 start { 0.0F };
  //! Stroke width in pixels.
  float width { 1.0F };
  glm::vec3 end { 0.0F };
  float pad0 { 0.0F };
  glm::vec4 color { 1.0F };
};
static_assert(sizeof(ViewOverlayLine) == 48U);

//! Overlay geometry drawn together: its triangles, then its lines.
struct ViewOverlayLayer {
  //! A triangle list.
  std::vector<ViewOverlayVertex> triangles;
  std::vector<ViewOverlayLine> lines;

  [[nodiscard]] auto IsEmpty() const noexcept -> bool
  {
    return triangles.empty() && lines.empty();
  }
};

//! Editor geometry a view draws over its post-processed output, outside
//! exposure and grading: transform gizmos and their drag feedback.
/*!
 The scene layer is depth tested against the scene: where scene geometry hides
 it, it is drawn with `occluded_alpha` instead of disappearing. The top layer
 is drawn afterwards over everything. Neither writes depth, and the overlay
 draws no geometry of its own; the caller supplies all of it every frame.
*/
struct ViewOverlay {
  ViewOverlayLayer scene;
  ViewOverlayLayer top;
  //! Opacity multiplier of scene-layer pixels behind scene geometry.
  float occluded_alpha { 0.3F };

  [[nodiscard]] auto IsEmpty() const noexcept -> bool
  {
    return scene.IsEmpty() && top.IsEmpty();
  }
};

} // namespace oxygen::vortex

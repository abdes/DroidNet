//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::vortex {

//! Scene nodes a view outlines: a screen-space band drawn around their
//! geometry after post-processing, outside exposure and grading.
/*!
 Only each listed node's own geometry is outlined; a caller that outlines a
 hierarchy lists its descendants. Occluded parts of the band are dimmed.
*/
struct ViewOutline {
  std::vector<scene::NodeHandle> nodes;
  //! Nodes drawn with `active_color`; a node in both lists is active.
  std::vector<scene::NodeHandle> active_nodes;
  graphics::Color color { 0.96F, 0.55F, 0.15F, 1.0F };
  graphics::Color active_color { 1.0F, 0.80F, 0.35F, 1.0F };
  //! Band width in pixels; at most three.
  float radius { 2.0F };
  //! Opacity of the band where the outlined surface is hidden.
  float occluded_alpha { 0.35F };

  [[nodiscard]] auto IsEmpty() const noexcept -> bool
  {
    return nodes.empty() && active_nodes.empty();
  }
};

} // namespace oxygen::vortex

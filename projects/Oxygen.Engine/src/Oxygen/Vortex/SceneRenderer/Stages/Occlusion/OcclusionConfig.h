//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::vortex {

//! Default `OcclusionConfig::depth_bias`: 0.1% of the occluder's depth.
inline constexpr float kDefaultOcclusionDepthBias = 1.0e-3F;

struct OcclusionConfig {
  //! Off: phase 1 draws every draw in the frustum and phase 2 does not run.
  bool enabled { false };
  //! Relative view-depth margin a box must lie behind its occluders to be
  //! culled.
  float depth_bias { kDefaultOcclusionDepthBias };
};

} // namespace oxygen::vortex

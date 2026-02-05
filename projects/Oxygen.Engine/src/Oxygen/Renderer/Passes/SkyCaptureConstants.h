//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/mat4x4.hpp>

namespace oxygen::engine {

//! Constants for a single cubemap face capture.
struct SkyCaptureFaceConstants {
  glm::mat4 view_matrix;
  glm::mat4 projection_matrix;
};

} // namespace oxygen::engine

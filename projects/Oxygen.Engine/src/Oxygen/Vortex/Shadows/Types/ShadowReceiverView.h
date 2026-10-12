//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex::shadows {

//! The receivers a camera view sees: its complete scene depth.
struct ShadowReceiverView {
  //! The view these receivers belong to.
  ViewId view_id { kInvalidViewId };
  observer_ptr<const graphics::Texture> scene_depth;
  ShaderVisibleIndex scene_depth_srv { kInvalidShaderVisibleIndex };
  //! The camera's projection (jitter included) times view, inverted.
  glm::mat4 inverse_view_projection { 1.0F };
  glm::mat4 view_matrix { 1.0F };
  //! The view rect in scene-depth pixels: origin x, origin y, width,
  //! height.
  glm::vec4 viewport { 0.0F };
  float near_plane { 0.0F };
};

} // namespace oxygen::vortex::shadows

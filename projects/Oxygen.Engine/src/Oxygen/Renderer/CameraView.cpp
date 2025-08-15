//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

using oxygen::engine::CameraView;

namespace {
[[nodiscard]] auto BuildViewMatrixFromPose(
  const glm::vec3& pos, const glm::quat& rot) -> glm::mat4
{
  const glm::vec3 forward = rot * glm::vec3(0.0F, 0.0F, -1.0F);
  const glm::vec3 up = rot * glm::vec3(0.0F, 1.0F, 0.0F);
  return glm::lookAt(pos, pos + forward, up);
}
} // namespace

auto CameraView::Resolve() const -> View
{
  using oxygen::scene::OrthographicCamera;
  using oxygen::scene::PerspectiveCamera;

  const auto& p = params_;
  // Remove the constness out of the camera_node because of potential lazy
  // invalidation wwhen accessing the node
  auto& camera_node
    = const_cast<std::remove_const_t<decltype(p.camera_node)>&>(p.camera_node);

  // Validate camera node
  if (!camera_node.IsAlive() || !camera_node.HasCamera()) {
    // Fallback to identity view/proj; caller can detect via culling results
    return View(View::Params {});
  }

  // Extract pose. Prefer world transform; if not available yet, fall back to
  // local.
  glm::vec3 cam_pos { 0.0F, 0.0F, 0.0F };
  glm::quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };
  if (auto wp = camera_node.GetTransform().GetWorldPosition()) {
    cam_pos = *wp;
  } else if (auto lp = camera_node.GetTransform().GetLocalPosition()) {
    cam_pos = *lp;
  }
  if (auto wr = camera_node.GetTransform().GetWorldRotation()) {
    cam_rot = *wr;
  } else if (auto lr = camera_node.GetTransform().GetLocalRotation()) {
    cam_rot = *lr;
  }

  const glm::mat4 view_m = BuildViewMatrixFromPose(cam_pos, cam_rot);

  // Projection from camera component
  glm::mat4 proj_m { 1.0F };
  if (auto cam = camera_node.GetCameraAs<PerspectiveCamera>()) {
    proj_m = cam->get().ProjectionMatrix();
  } else if (auto camo = camera_node.GetCameraAs<OrthographicCamera>()) {
    proj_m = camo->get().ProjectionMatrix();
  }

  View::Params vp;
  vp.view = view_m;
  vp.proj = proj_m;
  if (p.viewport) {
    vp.viewport = *p.viewport;
  } else {
    // Query camera for active viewport if perspective/ortho
    if (auto cam = camera_node.GetCameraAs<PerspectiveCamera>()) {
      vp.viewport = cam->get().ActiveViewport();
    } else if (auto camo = camera_node.GetCameraAs<OrthographicCamera>()) {
      vp.viewport = camo->get().ActiveViewport();
    }
  }
  if (p.scissor) {
    vp.scissor = *p.scissor;
  }
  vp.pixel_jitter = p.pixel_jitter;
  vp.reverse_z = p.reverse_z;
  vp.mirrored = p.mirrored;
  vp.has_camera_position = true;
  vp.camera_position = cam_pos;

  return View(vp);
}

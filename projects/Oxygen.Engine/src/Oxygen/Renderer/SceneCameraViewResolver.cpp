//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::renderer {

auto FromNodeLookup::ResolveForNode(scene::SceneNode& camera_node)
  -> ResolvedView
{
  // Validate camera node
  if (!camera_node.IsAlive() || !camera_node.HasCamera()) {
    ResolvedView::Params rp;
    rp.view_config = View {};
    rp.view_matrix = Mat4(1.0F);
    rp.proj_matrix = Mat4(1.0F);
    rp.depth_range = NdcDepthRange::ZeroToOne;
    return ResolvedView(rp);
  }

  // Extract pose
  Vec3 cam_pos { 0.0F, 0.0F, 0.0F };
  Quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };
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

  const auto view_m = [](const Vec3& pos, const Quat& rot) -> Mat4 {
    const Vec3 forward = rot * space::look::Forward;
    // Prefer World Up (Z+) to keep the camera stable, but fall back to the
    // camera's own up vector when looking nearly straight up/down.
    //
    // Using a fixed up vector makes `glm::lookAt` singular when `forward` is
    // colinear with `up` (e.g. Top/Bottom views), which can produce NaNs and
    // an effectively empty frame.
    Vec3 up = space::move::Up;
    const float forward_len2 = glm::dot(forward, forward);
    if (forward_len2 > 0.0F) {
      const float dot_abs
        = glm::abs(glm::dot(glm::normalize(forward), up));
      if (dot_abs > 0.999F) {
        up = rot * space::look::Up;
      }
    }

    return glm::lookAt(pos, pos + forward, up);
  }(cam_pos, cam_rot);

  // Projection from camera component
  Mat4 proj_m { 1.0F };
  // Engine canonical NDC: Z range [0,1]
  NdcDepthRange src_range = NdcDepthRange::ZeroToOne;
  if (auto cam = camera_node.GetCameraAs<scene::PerspectiveCamera>()) {
    proj_m = cam->get().ProjectionMatrix();
  } else if (auto camo = camera_node.GetCameraAs<scene::OrthographicCamera>()) {
    proj_m = camo->get().ProjectionMatrix();
  }

  // Build final view config: derive viewport from camera if present
  View cfg;
  if (auto cam = camera_node.GetCameraAs<scene::PerspectiveCamera>()) {
    cfg.viewport = cam->get().ActiveViewport();
  } else if (auto camo = camera_node.GetCameraAs<scene::OrthographicCamera>()) {
    cfg.viewport = camo->get().ActiveViewport();
  }

  // Apply pixel jitter (pixels -> NDC) and produce D3D12-targeted projection
  proj_m = ApplyJitterToProjection(proj_m, cfg.pixel_jitter, cfg.viewport);
  proj_m
    = RemapProjectionDepthRange(proj_m, src_range, NdcDepthRange::ZeroToOne);

  ResolvedView::Params rp;
  rp.view_config = cfg;
  rp.view_matrix = view_m;
  rp.proj_matrix = proj_m;
  rp.depth_range = NdcDepthRange::ZeroToOne;
  rp.camera_position = cam_pos;

  return ResolvedView(rp);
}

} // namespace oxygen::renderer

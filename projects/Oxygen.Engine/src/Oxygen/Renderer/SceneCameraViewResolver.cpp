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
    rp.near_plane = 0.1F;
    rp.far_plane = 1000.0F;
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
    // Use the camera's own orientation to define both forward and up.
    // This preserves roll and allows full inversion (world can appear upside
    // down) during orbit.
    const Vec3 forward = rot * space::look::Forward;
    const Vec3 up = rot * space::look::Up;
    return glm::lookAt(pos, pos + forward, up);
  }(cam_pos, cam_rot);

  // Projection from camera component
  Mat4 proj_m { 1.0F };
  float near_plane = 0.1F;
  float far_plane = 1000.0F;
  std::optional<float> camera_ev100 {};
  // Engine canonical NDC: Z range [0,1]
  NdcDepthRange src_range = NdcDepthRange::ZeroToOne;
  if (auto cam = camera_node.GetCameraAs<scene::PerspectiveCamera>()) {
    proj_m = cam->get().ProjectionMatrix();
    near_plane = cam->get().GetNearPlane();
    far_plane = cam->get().GetFarPlane();
    camera_ev100 = cam->get().Exposure().GetEv100();
  } else if (auto camo = camera_node.GetCameraAs<scene::OrthographicCamera>()) {
    proj_m = camo->get().ProjectionMatrix();
    const auto ext = camo->get().GetExtents();
    near_plane = ext[4];
    far_plane = ext[5];
    camera_ev100 = camo->get().Exposure().GetEv100();
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
  rp.camera_ev100 = camera_ev100;
  rp.near_plane = near_plane;
  rp.far_plane = far_plane;

  return ResolvedView(rp);
}

} // namespace oxygen::renderer

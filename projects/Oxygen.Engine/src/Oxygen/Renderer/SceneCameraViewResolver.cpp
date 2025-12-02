//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional>
#include <glm/glm.hpp>

#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::renderer {

auto FromNodeLookup::ResolveForNode(oxygen::scene::SceneNode& camera_node)
  -> oxygen::ResolvedView
{
  using namespace oxygen::scene::camera;

  // Validate camera node
  if (!camera_node.IsAlive() || !camera_node.HasCamera()) {
    oxygen::ResolvedView::Params rp;
    rp.view_config = oxygen::View {};
    rp.view_matrix = glm::mat4(1.0f);
    rp.proj_matrix = glm::mat4(1.0f);
    rp.depth_range = oxygen::NdcDepthRange::ZeroToOne;
    return oxygen::ResolvedView(rp);
  }

  // Extract pose
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

  const glm::mat4 view_m = [](const glm::vec3& pos, const glm::quat& rot) {
    const glm::vec3 forward = rot * glm::vec3(0.0F, 0.0F, -1.0F);
    const glm::vec3 up = rot * glm::vec3(0.0F, 1.0F, 0.0F);
    return glm::lookAt(pos, pos + forward, up);
  }(cam_pos, cam_rot);

  // Projection from camera component
  glm::mat4 proj_m { 1.0F };
  oxygen::NdcDepthRange src_range = oxygen::NdcDepthRange::MinusOneToOne;
  if (auto cam = camera_node.GetCameraAs<oxygen::scene::PerspectiveCamera>()) {
    proj_m = cam->get().ProjectionMatrix();
    // Derive NDC depth range from the camera's projection convention
    switch (cam->get().GetProjectionConvention()) {
    case ProjectionConvention::kD3D12:
      src_range = oxygen::NdcDepthRange::ZeroToOne;
      break;
    case ProjectionConvention::kVulkan:
      src_range = oxygen::NdcDepthRange::ZeroToOne;
      break;
    default:
      src_range = oxygen::NdcDepthRange::ZeroToOne;
      break;
    }
  } else if (auto camo
    = camera_node.GetCameraAs<oxygen::scene::OrthographicCamera>()) {
    proj_m = camo->get().ProjectionMatrix();
    switch (camo->get().GetProjectionConvention()) {
    case ProjectionConvention::kD3D12:
      src_range = oxygen::NdcDepthRange::ZeroToOne;
      break;
    case ProjectionConvention::kVulkan:
      src_range = oxygen::NdcDepthRange::ZeroToOne;
      break;
    default:
      src_range = oxygen::NdcDepthRange::ZeroToOne;
      break;
    }
  }

  // Build final view config: derive viewport from camera if present
  oxygen::View cfg;
  if (auto cam = camera_node.GetCameraAs<oxygen::scene::PerspectiveCamera>()) {
    cfg.viewport = cam->get().ActiveViewport();
  } else if (auto camo
    = camera_node.GetCameraAs<oxygen::scene::OrthographicCamera>()) {
    cfg.viewport = camo->get().ActiveViewport();
  }

  // Apply pixel jitter (pixels -> NDC) and produce D3D12-targeted projection
  proj_m
    = oxygen::ApplyJitterToProjection(proj_m, cfg.pixel_jitter, cfg.viewport);
  proj_m = oxygen::RemapProjectionDepthRange(
    proj_m, src_range, oxygen::NdcDepthRange::ZeroToOne);

  oxygen::ResolvedView::Params rp;
  rp.view_config = cfg;
  rp.view_matrix = view_m;
  rp.proj_matrix = proj_m;
  rp.depth_range = oxygen::NdcDepthRange::ZeroToOne;
  rp.camera_position = cam_pos;

  return oxygen::ResolvedView(rp);
}

} // namespace oxygen::renderer

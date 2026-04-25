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
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>

namespace oxygen::vortex {
namespace {

constexpr float kDefaultNearPlane = 0.1F;
constexpr float kDefaultFarPlane = 1000.0F;
constexpr std::size_t kOrthographicFarExtentIndex = 5U;

auto ResolveDefaultScissor(const ViewPort& viewport) -> Scissors
{
  return {
    .left = static_cast<int32_t>(viewport.top_left_x),
    .top = static_cast<int32_t>(viewport.top_left_y),
    .right = static_cast<int32_t>(viewport.top_left_x + viewport.width),
    .bottom = static_cast<int32_t>(viewport.top_left_y + viewport.height),
  };
}

} // namespace

auto FromNodeLookup::ResolveForNode(scene::SceneNode& camera_node,
  std::optional<oxygen::ViewPort> viewport_override) -> ResolvedView
{
  if (!camera_node.IsAlive() || !camera_node.HasCamera()) {
    ResolvedView::Params params;
    params.view_config = View {};
    params.view_matrix = Mat4(1.0F);
    params.proj_matrix = Mat4(1.0F);
    params.depth_range = NdcDepthRange::ZeroToOne;
    params.near_plane = kDefaultNearPlane;
    params.far_plane = kDefaultFarPlane;
    return ResolvedView(params);
  }

  Vec3 cam_pos { 0.0F, 0.0F, 0.0F };
  Quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };
  if (!camera_node.HasParent()) {
    if (auto lp = camera_node.GetTransform().GetLocalPosition()) {
      cam_pos = *lp;
    }
    if (auto lr = camera_node.GetTransform().GetLocalRotation()) {
      cam_rot = *lr;
    }
  } else {
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
  }

  const auto view_m = [](const Vec3& pos, const Quat& rot) -> Mat4 {
    const Vec3 forward = rot * space::look::Forward;
    const Vec3 up = rot * space::look::Up;
    return glm::lookAt(pos, pos + forward, up);
  }(cam_pos, cam_rot);

  Mat4 proj_m { 1.0F };
  float near_plane = kDefaultNearPlane;
  float far_plane = kDefaultFarPlane;
  std::optional<float> camera_ev {};
  NdcDepthRange src_range = NdcDepthRange::ZeroToOne;
  if (auto cam = camera_node.GetCameraAs<scene::PerspectiveCamera>()) {
    proj_m = cam->get().ProjectionMatrix();
    near_plane = cam->get().GetNearPlane();
    far_plane = cam->get().GetFarPlane();
    camera_ev = cam->get().Exposure().GetEv();
  } else if (auto camo = camera_node.GetCameraAs<scene::OrthographicCamera>()) {
    proj_m = camo->get().ProjectionMatrix();
    const auto ext = camo->get().GetExtents();
    near_plane = ext[4];
    far_plane = ext[kOrthographicFarExtentIndex];
    camera_ev = camo->get().Exposure().GetEv();
  }

  View cfg;
  cfg.reverse_z = true;
  if (auto cam = camera_node.GetCameraAs<scene::PerspectiveCamera>()) {
    cfg.viewport = cam->get().ActiveViewport();
  } else if (auto camo = camera_node.GetCameraAs<scene::OrthographicCamera>()) {
    cfg.viewport = camo->get().ActiveViewport();
  }
  if (viewport_override.has_value() && viewport_override->IsValid()) {
    cfg.viewport = *viewport_override;
  }
  if (cfg.scissor.right <= cfg.scissor.left
    || cfg.scissor.bottom <= cfg.scissor.top) {
    cfg.scissor = ResolveDefaultScissor(cfg.viewport);
  }

  const auto stable_proj_m
    = RemapProjectionDepthRange(proj_m, src_range, NdcDepthRange::ZeroToOne);

  proj_m = ApplyJitterToProjection(proj_m, cfg.pixel_jitter, cfg.viewport);
  proj_m
    = RemapProjectionDepthRange(proj_m, src_range, NdcDepthRange::ZeroToOne);

  ResolvedView::Params params;
  params.view_config = cfg;
  params.view_matrix = view_m;
  params.proj_matrix = proj_m;
  params.stable_proj_matrix = stable_proj_m;
  params.depth_range = NdcDepthRange::ZeroToOne;
  params.camera_position = cam_pos;
  params.camera_ev = camera_ev;
  params.near_plane = near_plane;
  params.far_plane = far_plane;
  return ResolvedView(params);
}

} // namespace oxygen::vortex

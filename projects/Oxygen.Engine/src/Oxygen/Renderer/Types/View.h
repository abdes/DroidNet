//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>

#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Renderer/Types/Frustum.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Immutable per-frame view snapshot used for rendering and culling.
/*!
 Holds camera matrices, derived cached inverses and the frustum. Values are
 immutable after construction.

 Inputs:
 - view, proj matrices
 - optional viewport/scissor
 - pixel_jitter (default 0,0)
 - reverse_z (default false), mirrored (default false)
 - camera_position (optional; inferred from inverse(view) if not provided)

 Derived:
 - inv_view, inv_proj, view_proj, inv_view_proj, frustum

 @see Frustum
 */
class View {
public:
  struct Params {
    glm::mat4 view { 1.0f };
    glm::mat4 proj { 1.0f };
    ViewPort viewport {};
    Scissors scissor {};
    glm::vec2 pixel_jitter { 0.0f, 0.0f };
    bool reverse_z = false;
    bool mirrored = false;
    bool has_camera_position = false;
    glm::vec3 camera_position { 0.0f, 0.0f, 0.0f };
  };

  OXGN_RNDR_API explicit View(const Params& p);

  // Getters (immutable)
  [[nodiscard]] auto ViewMatrix() const noexcept { return view_; }
  [[nodiscard]] auto ProjectionMatrix() const noexcept { return proj_; }
  [[nodiscard]] auto InverseView() const noexcept { return inv_view_; }
  [[nodiscard]] auto InverseProjection() const noexcept { return inv_proj_; }
  [[nodiscard]] auto ViewProjection() const noexcept { return view_proj_; }
  [[nodiscard]] auto InverseViewProjection() const noexcept
  {
    return inv_view_proj_;
  }
  [[nodiscard]] auto GetFrustum() const noexcept { return frustum_; }
  [[nodiscard]] auto Viewport() const noexcept { return viewport_; }
  [[nodiscard]] auto Scissor() const noexcept { return scissor_; }
  [[nodiscard]] auto PixelJitter() const noexcept { return pixel_jitter_; }
  [[nodiscard]] auto ReverseZ() const noexcept { return reverse_z_; }
  [[nodiscard]] auto Mirrored() const noexcept { return mirrored_; }
  [[nodiscard]] auto CameraPosition() const noexcept
  {
    return camera_position_;
  }
  //! Vertical focal length in pixels derived from projection and viewport.
  [[nodiscard]] auto FocalLengthPixels() const noexcept
  {
    return focal_length_pixels_;
  }

private:
  glm::mat4 view_ { 1.0f };
  glm::mat4 proj_ { 1.0f };
  glm::mat4 inv_view_ { 1.0f };
  glm::mat4 inv_proj_ { 1.0f };
  glm::mat4 view_proj_ { 1.0f };
  glm::mat4 inv_view_proj_ { 1.0f };

  ViewPort viewport_ {};
  Scissors scissor_ {};
  glm::vec2 pixel_jitter_ { 0.0f, 0.0f };
  bool reverse_z_ = false;
  bool mirrored_ = false;
  glm::vec3 camera_position_ { 0.0f, 0.0f, 0.0f };

  // Cached vertical focal length in pixels (or pixels-per-world-unit for
  // orthographic projections). Computed from proj_[1][1] and viewport height.
  float focal_length_pixels_ { 0.0f };

  Frustum frustum_ {};
};

} // namespace oxygen::engine

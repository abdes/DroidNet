//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>
#include <limits>
#include <optional>

#include <Oxygen/Core/Types/Frustum.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen {

enum class NdcDepthRange { MinusOneToOne, ZeroToOne };

class ResolvedView {
public:
  struct Params {
    View view_config {};
    glm::mat4 view_matrix { 1.0f };
    glm::mat4 proj_matrix { 1.0f };
    std::optional<glm::vec3> camera_position {};
    std::optional<float> camera_ev100 {};
    NdcDepthRange depth_range = NdcDepthRange::ZeroToOne; // default D3D

    // Camera clip planes in view-space units.
    // These must reflect the camera used to build the projection matrix.
    float near_plane = 0.1F;
    float far_plane = 1000.0F;
  };

  OXGN_CORE_API explicit ResolvedView(const Params& p);

  [[nodiscard]] auto Config() const noexcept -> View { return config_; }
  [[nodiscard]] auto ViewMatrix() const noexcept -> glm::mat4 { return view_; }
  [[nodiscard]] auto ProjectionMatrix() const noexcept -> glm::mat4
  {
    return proj_;
  }
  [[nodiscard]] auto InverseView() const noexcept -> glm::mat4
  {
    return inv_view_;
  }
  [[nodiscard]] auto InverseProjection() const noexcept -> glm::mat4
  {
    return inv_proj_;
  }
  [[nodiscard]] auto ViewProjection() const noexcept -> glm::mat4
  {
    return view_proj_;
  }
  [[nodiscard]] auto InverseViewProjection() const noexcept -> glm::mat4
  {
    return inv_view_proj_;
  }
  [[nodiscard]] auto GetFrustum() const noexcept -> Frustum { return frustum_; }
  [[nodiscard]] auto Viewport() const noexcept -> ViewPort { return viewport_; }
  [[nodiscard]] auto Scissor() const noexcept -> Scissors { return scissor_; }
  [[nodiscard]] auto PixelJitter() const noexcept -> glm::vec2
  {
    return pixel_jitter_;
  }
  [[nodiscard]] auto ReverseZ() const noexcept -> bool { return reverse_z_; }
  [[nodiscard]] auto Mirrored() const noexcept -> bool { return mirrored_; }
  [[nodiscard]] auto CameraPosition() const noexcept -> glm::vec3
  {
    return camera_position_;
  }
  [[nodiscard]] auto CameraEv100() const noexcept -> std::optional<float>
  {
    return camera_ev100_;
  }
  [[nodiscard]] auto FocalLengthPixels() const noexcept -> float
  {
    return focal_length_pixels_;
  }
  [[nodiscard]] auto DepthRange() const noexcept -> NdcDepthRange
  {
    return depth_range_;
  }

  [[nodiscard]] auto NearPlane() const noexcept -> float { return near_plane_; }
  [[nodiscard]] auto FarPlane() const noexcept -> float { return far_plane_; }

private:
  View config_ {};
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
  std::optional<float> camera_ev100_ {};

  float near_plane_ { 0.1F };
  float far_plane_ { 1000.0F };

  float focal_length_pixels_ = 0.0f;
  Frustum frustum_ {};
  NdcDepthRange depth_range_ = NdcDepthRange::ZeroToOne;
};

} // namespace oxygen

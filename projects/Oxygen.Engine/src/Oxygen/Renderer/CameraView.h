//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>
#include <optional>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Renderer/Errors.h>
#include <Oxygen/Renderer/api_export.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::renderer {

//! Non-owning descriptor of a camera-driven view.
/*! Resolves a per-frame immutable View snapshot from a scene camera node.
    The scene's transforms must be up-to-date for the current frame before
    calling Resolve(). */
class CameraView : public engine::RenderableView {
public:
  struct Params {
    // Camera node handle (non-owning). Must have a camera component.
    oxygen::scene::SceneNode camera_node;
    // Optional overrides; if not set, camera's ActiveViewport() is used.
    std::optional<ViewPort> viewport {};
    std::optional<Scissors> scissor {};
    glm::vec2 pixel_jitter { 0.0f, 0.0f };
    bool reverse_z = false;
    bool mirrored = false;
  };

  OXGN_RNDR_API explicit CameraView(
    Params p, std::weak_ptr<graphics::Surface> surface);

  OXYGEN_DEFAULT_COPYABLE(CameraView)
  OXYGEN_DEFAULT_MOVABLE(CameraView)

  ~CameraView() override = default;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return name_;
  }

  auto SetName(std::string_view name) noexcept -> void override
  {
    name_ = std::move(name);
  }

  [[nodiscard]] auto GetSurface() const noexcept
    -> std::expected<std::reference_wrapper<graphics::Surface>, std::string>
  {
    if (auto surface = surface_.lock()) {
      return std::ref(*surface);
    }
    return std::unexpected(
      make_error_code(RendererError::kSurfaceExpired).message());
  }

  // Builds a View snapshot from the camera's world transform and projection.
  // Contract: Scene transforms must have been updated prior to this call.
  OXGN_RNDR_NDAPI auto Resolve() const noexcept -> View override;

  [[nodiscard]] auto GetParams() const noexcept -> const Params&
  {
    return params_;
  }

private:
  Params params_;
  std::string name_;
  std::weak_ptr<graphics::Surface> surface_;
};

} // namespace oxygen::renderer

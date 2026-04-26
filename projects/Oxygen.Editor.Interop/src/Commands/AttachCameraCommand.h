//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class AttachPerspectiveCameraCommand final : public EditorCommand {
  public:
    AttachPerspectiveCameraCommand(oxygen::scene::NodeHandle node,
      float field_of_view_y_radians, float aspect_ratio, float near_plane,
      float far_plane)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , field_of_view_y_radians_(field_of_view_y_radians)
      , aspect_ratio_(aspect_ratio)
      , near_plane_(near_plane)
      , far_plane_(far_plane)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }

      auto scene_node_opt = context.Scene->GetNode(node_);
      if (!scene_node_opt || !scene_node_opt->IsAlive()) {
        return;
      }

      auto camera = std::make_unique<oxygen::scene::PerspectiveCamera>();
      camera->SetFieldOfView(SanitizeFieldOfView(field_of_view_y_radians_));
      camera->SetAspectRatio(SanitizeAspectRatio(aspect_ratio_));

      const auto near_plane = SanitizeNearPlane(near_plane_);
      camera->SetNearPlane(near_plane);
      camera->SetFarPlane(SanitizeFarPlane(far_plane_, near_plane));

      (void)scene_node_opt->ReplaceCamera(std::move(camera));
    }

  private:
    static auto SanitizeFieldOfView(float value) noexcept -> float
    {
      constexpr float kDefault = 1.0471975512F; // 60 degrees.
      constexpr float kMin = 0.0174532925F; // 1 degree.
      constexpr float kMax = 3.1241393611F; // 179 degrees.
      if (!std::isfinite(value) || value <= 0.0F) {
        return kDefault;
      }
      return std::clamp(value, kMin, kMax);
    }

    static auto SanitizeAspectRatio(float value) noexcept -> float
    {
      constexpr float kDefault = 16.0F / 9.0F;
      if (!std::isfinite(value) || value <= 0.0F) {
        return kDefault;
      }
      return value;
    }

    static auto SanitizeNearPlane(float value) noexcept -> float
    {
      if (!std::isfinite(value) || value <= 0.0F) {
        return 0.1F;
      }
      return value;
    }

    static auto SanitizeFarPlane(float value, float near_plane) noexcept -> float
    {
      if (!std::isfinite(value) || value <= near_plane) {
        return std::max(near_plane + 1.0F, 1000.0F);
      }
      return value;
    }

    oxygen::scene::NodeHandle node_;
    float field_of_view_y_radians_ = 1.0471975512F;
    float aspect_ratio_ = 16.0F / 9.0F;
    float near_plane_ = 0.1F;
    float far_plane_ = 1000.0F;
  };

  class DetachCameraCommand final : public EditorCommand {
  public:
    explicit DetachCameraCommand(oxygen::scene::NodeHandle node)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }

      auto scene_node_opt = context.Scene->GetNode(node_);
      if (!scene_node_opt || !scene_node_opt->IsAlive()) {
        return;
      }

      (void)scene_node_opt->DetachCamera();
    }

  private:
    oxygen::scene::NodeHandle node_;
  };

} // namespace oxygen::interop::module

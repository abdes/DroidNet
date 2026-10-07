//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <memory>
#include <numbers>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class AttachPerspectiveCameraCommand final : public EditorCommand {
  public:
    AttachPerspectiveCameraCommand(oxygen::scene::NodeHandle node,
      float field_of_view_y_radians, float aspect_ratio, float near_plane,
      float far_plane, oxygen::CameraAspectMode aspect_mode)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , field_of_view_y_radians_(field_of_view_y_radians)
      , aspect_ratio_(aspect_ratio)
      , near_plane_(near_plane)
      , far_plane_(far_plane)
      , aspect_mode_(aspect_mode)
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

      if (!std::isfinite(field_of_view_y_radians_)
        || field_of_view_y_radians_ <= 0.0F
        || field_of_view_y_radians_ >= std::numbers::pi_v<float>
        || !std::isfinite(aspect_ratio_) || aspect_ratio_ <= 0.0F
        || !std::isfinite(near_plane_) || near_plane_ <= 0.0F
        || !std::isfinite(far_plane_) || far_plane_ <= near_plane_
        || (aspect_mode_ != oxygen::CameraAspectMode::kAuto
          && aspect_mode_ != oxygen::CameraAspectMode::kFixed)) {
        LOG_F(ERROR, "Rejected invalid perspective camera projection");
        return;
      }

      auto camera = std::make_unique<oxygen::scene::PerspectiveCamera>();
      camera->SetFieldOfView(field_of_view_y_radians_);
      camera->SetAspectRatio(aspect_ratio_);
      camera->SetAspectMode(aspect_mode_);
      camera->SetNearPlane(near_plane_);
      camera->SetFarPlane(far_plane_);

      (void)scene_node_opt->ReplaceCamera(std::move(camera));
    }

  private:
    oxygen::scene::NodeHandle node_;
    float field_of_view_y_radians_ = 1.0471975512F;
    float aspect_ratio_ = 16.0F / 9.0F;
    float near_plane_ = 0.1F;
    float far_plane_ = 1000.0F;
    oxygen::CameraAspectMode aspect_mode_ = oxygen::CameraAspectMode::kAuto;
  };

  class AttachOrthographicCameraCommand final : public EditorCommand {
  public:
    AttachOrthographicCameraCommand(oxygen::scene::NodeHandle node,
      float orthographic_size, float aspect_ratio, float near_plane,
      float far_plane, oxygen::CameraAspectMode aspect_mode)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , orthographic_size_(orthographic_size)
      , aspect_ratio_(aspect_ratio)
      , near_plane_(near_plane)
      , far_plane_(far_plane)
      , aspect_mode_(aspect_mode)
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

      if (!std::isfinite(orthographic_size_) || orthographic_size_ <= 0.0F
        || !std::isfinite(aspect_ratio_) || aspect_ratio_ <= 0.0F
        || !std::isfinite(near_plane_) || near_plane_ <= 0.0F
        || !std::isfinite(far_plane_) || far_plane_ <= near_plane_
        || (aspect_mode_ != oxygen::CameraAspectMode::kAuto
          && aspect_mode_ != oxygen::CameraAspectMode::kFixed)) {
        LOG_F(ERROR, "Rejected invalid orthographic camera projection");
        return;
      }

      auto camera = std::make_unique<oxygen::scene::OrthographicCamera>();
      camera->SetOrthographicSize(orthographic_size_);
      camera->SetAspectRatio(aspect_ratio_);
      camera->SetAspectMode(aspect_mode_);
      camera->SetNearPlane(near_plane_);
      camera->SetFarPlane(far_plane_);

      (void)scene_node_opt->ReplaceCamera(std::move(camera));
    }

  private:
    oxygen::scene::NodeHandle node_;
    float orthographic_size_ = 10.0F;
    float aspect_ratio_ = 16.0F / 9.0F;
    float near_plane_ = 0.1F;
    float far_plane_ = 1000.0F;
    oxygen::CameraAspectMode aspect_mode_ = oxygen::CameraAspectMode::kAuto;
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

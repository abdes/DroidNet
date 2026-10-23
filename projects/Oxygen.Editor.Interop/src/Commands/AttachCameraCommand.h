//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>
#include <memory>
#include <stdexcept>

#include <Oxygen/Core/Types/CameraAspectMode.h>

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <Commands/OrthographicCameraPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  //! Whether `mode` is a member of the schema's `CameraAspectMode` for the
  //! aspect-mode field of `component`.
  [[nodiscard]] inline auto IsValidAspectMode(const ComponentId component,
    const std::uint16_t field, const CameraAspectMode mode) -> bool
  {
    return IsInRange(PropertyEntry {
      .component = component,
      .field = field,
      .value = static_cast<std::int32_t>(mode),
    });
  }

  //! Attaches or replaces the perspective camera of one node.
  /*!
   The projection is validated when the command is made, so the caller sees
   the refusal; each value against its field, then far beyond near.

   \throws std::invalid_argument when a value is outside its field's range or
   the far plane is not beyond the near plane.
  */
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
      const auto valid = [](const PerspectiveCameraField field,
                           const float value) {
        return IsInRange(PropertyEntry {
          .component = ComponentId::kPerspectiveCamera,
          .field = static_cast<std::uint16_t>(field),
          .value = value,
        });
      };
      if (!valid(PerspectiveCameraField::kFieldOfViewYRadians,
            field_of_view_y_radians_)
        || !valid(PerspectiveCameraField::kAspectRatio, aspect_ratio_)
        || !valid(PerspectiveCameraField::kNearPlane, near_plane_)
        || !valid(PerspectiveCameraField::kFarPlane, far_plane_)
        || !(far_plane_ > near_plane_)
        || !IsValidAspectMode(ComponentId::kPerspectiveCamera,
          static_cast<std::uint16_t>(PerspectiveCameraField::kAspectMode),
          aspect_mode_)) {
        throw std::invalid_argument("Invalid perspective camera projection");
      }
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
      camera->SetFieldOfView(field_of_view_y_radians_);
      camera->SetAspectRatio(aspect_ratio_);
      camera->SetAspectMode(aspect_mode_);
      camera->SetNearPlane(near_plane_);
      camera->SetFarPlane(far_plane_);

      (void)scene_node_opt->ReplaceCamera(std::move(camera));
    }

  private:
    oxygen::scene::NodeHandle node_;
    float field_of_view_y_radians_;
    float aspect_ratio_;
    float near_plane_;
    float far_plane_;
    oxygen::CameraAspectMode aspect_mode_;
  };

  //! Attaches or replaces the orthographic camera of one node.
  /*!
   The projection is validated when the command is made, so the caller sees
   the refusal.

   \throws std::invalid_argument when the projection or the aspect mode is
   not valid.
  */
  class AttachOrthographicCameraCommand final : public EditorCommand {
  public:
    AttachOrthographicCameraCommand(oxygen::scene::NodeHandle node,
      const OrthographicCameraProjection& projection,
      oxygen::CameraAspectMode aspect_mode)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , projection_(projection)
      , aspect_mode_(aspect_mode)
    {
      if (!projection_.IsValid()
        || !IsValidAspectMode(ComponentId::kOrthographicCamera,
          static_cast<std::uint16_t>(OrthographicCameraField::kAspectMode),
          aspect_mode_)) {
        throw std::invalid_argument("Invalid orthographic camera projection");
      }
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

      auto camera = std::make_unique<oxygen::scene::OrthographicCamera>();
      projection_.ApplyTo(*camera);
      camera->SetAspectMode(aspect_mode_);

      (void)scene_node_opt->ReplaceCamera(std::move(camera));
    }

  private:
    oxygen::scene::NodeHandle node_;
    OrthographicCameraProjection projection_;
    oxygen::CameraAspectMode aspect_mode_;
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

#pragma managed(pop)

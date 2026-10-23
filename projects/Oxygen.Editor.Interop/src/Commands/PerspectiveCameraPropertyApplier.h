//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Commands/SchemaEnums.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  [[nodiscard]] inline auto to_string(const PerspectiveCameraField field) noexcept
    -> std::string_view
  {
    switch (field) {
    case PerspectiveCameraField::kFieldOfViewYRadians: return "FieldOfViewYRadians";
    case PerspectiveCameraField::kAspectRatio: return "AspectRatio";
    case PerspectiveCameraField::kNearPlane: return "NearPlane";
    case PerspectiveCameraField::kFarPlane: return "FarPlane";
    case PerspectiveCameraField::kApertureF: return "ApertureF";
    case PerspectiveCameraField::kShutterRate: return "ShutterRate";
    case PerspectiveCameraField::kIso: return "Iso";
    case PerspectiveCameraField::kAspectMode: return "AspectMode";
    case PerspectiveCameraField::kCount: break;
    }
    return "__NotSupported__";
  }

  //! Applies §5.3 property entries to `oxygen::scene::PerspectiveCamera`.
  class PerspectiveCameraPropertyApplier final
    : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept
      -> ComponentId override
    {
      return ComponentId::kPerspectiveCamera;
    }

    void Apply(oxygen::scene::SceneNode& node,
      std::span<const PropertyEntry> entries) override
    {
      auto camera_ref = node.GetCameraAs<oxygen::scene::PerspectiveCamera>();
      if (!camera_ref) {
        throw std::invalid_argument("The node has no perspective camera");
      }
      auto& camera = camera_ref->get();

      // The whole run is checked before the camera changes: each value
      // against its field, then the candidate's planes against each other.
      float near_plane = camera.GetNearPlane();
      float far_plane = camera.GetFarPlane();
      for (const auto& entry : entries) {
        const auto field = static_cast<PerspectiveCameraField>(entry.field);
        if (!IsInRange(entry)) {
          throw std::invalid_argument(std::string { to_string(field) }
            + ": camera property value is not valid for its field");
        }
        if (field == PerspectiveCameraField::kNearPlane) {
          near_plane = entry.Float();
        } else if (field == PerspectiveCameraField::kFarPlane) {
          far_plane = entry.Float();
        }
      }
      if (!(far_plane > near_plane)) {
        throw std::invalid_argument(
          "The perspective camera far plane must be beyond its near plane");
      }

      for (const auto& entry : entries) {
        const auto field = static_cast<PerspectiveCameraField>(entry.field);
        switch (field) {
        case PerspectiveCameraField::kAspectMode:
          camera.SetAspectMode(
            ValidatedMember(ToCameraAspectMode(entry.Int32())));
          break;
        case PerspectiveCameraField::kFieldOfViewYRadians:
          camera.SetFieldOfView(entry.Float());
          break;
        case PerspectiveCameraField::kAspectRatio:
          camera.SetAspectRatio(entry.Float());
          break;
        case PerspectiveCameraField::kNearPlane:
          camera.SetNearPlane(entry.Float());
          break;
        case PerspectiveCameraField::kFarPlane:
          camera.SetFarPlane(entry.Float());
          break;
        case PerspectiveCameraField::kApertureF:
        case PerspectiveCameraField::kShutterRate:
        case PerspectiveCameraField::kIso: {
          auto exposure = camera.Exposure();
          if (field == PerspectiveCameraField::kApertureF) {
            exposure.aperture_f = entry.Float();
          } else if (field == PerspectiveCameraField::kShutterRate) {
            exposure.shutter_rate = entry.Float();
          } else {
            exposure.iso = entry.Float();
          }
          camera.SetExposure(exposure);
          break;
        }
        case PerspectiveCameraField::kCount:
          break;
        }
      }
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

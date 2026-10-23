//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Base/Logging.h>
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
        LOG_F(WARNING,
          "PerspectiveCameraPropertyApplier skipped {} entries: node has no "
          "PerspectiveCamera",
          entries.size());
        return;
      }

      auto& camera = camera_ref->get();
      for (const auto& entry : entries) {
        const auto field = static_cast<PerspectiveCameraField>(entry.field);
        if (field == PerspectiveCameraField::kAspectMode) {
          const auto mode = entry.Int32();
          if (mode == 0 || mode == 1) {
            camera.SetAspectMode(mode == 0 ? oxygen::CameraAspectMode::kAuto
                                           : oxygen::CameraAspectMode::kFixed);
          }
          continue;
        }
        const float value = entry.Float();
        if (!std::isfinite(value)) {
          LOG_F(WARNING,
            "PerspectiveCameraPropertyApplier skipped field {}: non-finite "
            "value {}",
            entry.field, value);
          continue;
        }

        switch (field) {
        case PerspectiveCameraField::kFieldOfViewYRadians:
          camera.SetFieldOfView(value);
          break;
        case PerspectiveCameraField::kAspectRatio:
          if (value > 0.0F) {
            camera.SetAspectRatio(value);
          }
          break;
        case PerspectiveCameraField::kNearPlane:
          if (value > 0.0F) {
            camera.SetNearPlane(value);
          }
          break;
        case PerspectiveCameraField::kFarPlane:
          if (value > 0.0F) {
            camera.SetFarPlane(value);
          }
          break;
        case PerspectiveCameraField::kApertureF:
        case PerspectiveCameraField::kShutterRate:
        case PerspectiveCameraField::kIso: {
          if (value <= 0.0F) {
            LOG_F(WARNING, "Camera exposure edit rejected: {} must be positive (received {})",
              to_string(field), value);
            break;
          }
          auto exposure = camera.Exposure();
          if (field == PerspectiveCameraField::kApertureF) {
            exposure.aperture_f = value;
          } else if (field == PerspectiveCameraField::kShutterRate) {
            exposure.shutter_rate = value;
          } else {
            exposure.iso = value;
          }
          camera.SetExposure(exposure);
          break;
        }
        default:
          LOG_F(WARNING,
            "PerspectiveCameraPropertyApplier skipped unknown field id {}",
            entry.field);
          break;
        }
      }
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

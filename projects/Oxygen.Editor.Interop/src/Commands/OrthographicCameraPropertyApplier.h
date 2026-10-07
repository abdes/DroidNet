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
#include <stdexcept>
#include <string>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  enum class OrthographicCameraField : std::uint16_t {
    kOrthographicSize = 0,
    kAspectRatio = 1,
    kNearPlane = 2,
    kFarPlane = 3,
    kApertureF = 4,
    kShutterRate = 5,
    kIso = 6,
    kAspectMode = 7,
    kCount,
  };

  //! Editor view of an orthographic volume: half-height, Fixed width ratio.
  struct OrthographicCameraProjection {
    float orthographic_size { 0.0F };
    float aspect_ratio { 0.0F };
    float near_plane { 0.0F };
    float far_plane { 0.0F };

    [[nodiscard]] static auto From(const scene::OrthographicCamera& camera)
      -> OrthographicCameraProjection
    {
      const auto extents = camera.GetExtents();
      const float half_height = 0.5F * (extents[3] - extents[2]);
      return {
        .orthographic_size = half_height,
        .aspect_ratio = (extents[1] - extents[0]) / (2.0F * half_height),
        .near_plane = extents[4],
        .far_plane = extents[5],
      };
    }

    [[nodiscard]] auto IsValid() const noexcept -> bool
    {
      return std::isfinite(orthographic_size) && orthographic_size > 0.0F
        && std::isfinite(aspect_ratio) && aspect_ratio > 0.0F
        && std::isfinite(near_plane) && near_plane > 0.0F
        && std::isfinite(far_plane) && far_plane > near_plane;
    }

    auto ApplyTo(scene::OrthographicCamera& camera) const -> void
    {
      const float half_width = orthographic_size * aspect_ratio;
      camera.SetExtents(-half_width, half_width, -orthographic_size,
        orthographic_size, near_plane, far_plane);
    }
  };

  //! Applies one complete, validated orthographic candidate atomically.
  class OrthographicCameraPropertyApplier final
    : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept -> ComponentId override
    {
      return ComponentId::kOrthographicCamera;
    }

    void Apply(scene::SceneNode& node,
      std::span<const PropertyEntry> entries) override
    {
      auto camera_ref = node.GetCameraAs<scene::OrthographicCamera>();
      if (!camera_ref) {
        throw std::invalid_argument("The node has no orthographic camera");
      }

      auto& camera = camera_ref->get();
      auto projection = OrthographicCameraProjection::From(camera);
      auto mode = camera.GetAspectMode();
      auto exposure = camera.Exposure();
      for (const auto& entry : entries) {
        const auto field = static_cast<OrthographicCameraField>(entry.field);
        const float value = entry.value;
        if (!std::isfinite(value)) {
          throw std::invalid_argument(
            std::to_string(entry.field) + ": non-finite camera value");
        }
        switch (field) {
        case OrthographicCameraField::kOrthographicSize:
          projection.orthographic_size = value;
          break;
        case OrthographicCameraField::kAspectRatio:
          projection.aspect_ratio = value;
          break;
        case OrthographicCameraField::kNearPlane:
          projection.near_plane = value;
          break;
        case OrthographicCameraField::kFarPlane:
          projection.far_plane = value;
          break;
        case OrthographicCameraField::kApertureF:
          exposure.aperture_f = value;
          break;
        case OrthographicCameraField::kShutterRate:
          exposure.shutter_rate = value;
          break;
        case OrthographicCameraField::kIso:
          exposure.iso = value;
          break;
        case OrthographicCameraField::kAspectMode:
          if (value != 0.0F && value != 1.0F) {
            throw std::invalid_argument("Invalid camera aspect mode");
          }
          mode = value == 0.0F ? CameraAspectMode::kAuto
                               : CameraAspectMode::kFixed;
          break;
        default:
          throw std::invalid_argument(
            std::to_string(entry.field) + ": unknown camera property");
        }
      }

      if (!projection.IsValid() || exposure.aperture_f <= 0.0F
        || exposure.shutter_rate <= 0.0F || exposure.iso <= 0.0F) {
        throw std::invalid_argument("Invalid orthographic camera candidate");
      }
      projection.ApplyTo(camera);
      camera.SetAspectMode(mode);
      camera.SetExposure(exposure);
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

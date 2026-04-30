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

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  enum class PerspectiveCameraField : std::uint16_t {
    kFieldOfViewYRadians = 0,
    kAspectRatio = 1,
    kNearPlane = 2,
    kFarPlane = 3,
    kCount,
  };

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
        if (!std::isfinite(entry.value)) {
          LOG_F(WARNING,
            "PerspectiveCameraPropertyApplier skipped field {}: non-finite "
            "value {}",
            entry.field, entry.value);
          continue;
        }

        switch (static_cast<PerspectiveCameraField>(entry.field)) {
        case PerspectiveCameraField::kFieldOfViewYRadians:
          camera.SetFieldOfView(entry.value);
          break;
        case PerspectiveCameraField::kAspectRatio:
          if (entry.value > 0.0F) {
            camera.SetAspectRatio(entry.value);
          }
          break;
        case PerspectiveCameraField::kNearPlane:
          if (entry.value > 0.0F) {
            camera.SetNearPlane(entry.value);
          }
          break;
        case PerspectiveCameraField::kFarPlane:
          if (entry.value > 0.0F) {
            camera.SetFarPlane(entry.value);
          }
          break;
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

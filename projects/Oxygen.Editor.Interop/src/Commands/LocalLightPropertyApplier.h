//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cmath>
#include <concepts>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  template <typename T>
  concept LocalLight
    = std::same_as<T, scene::PointLight> || std::same_as<T, scene::SpotLight>;

  //! Applies one complete point/spot candidate; validation rejects it whole.
  template <LocalLight T>
  class LocalLightPropertyApplier final : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept -> ComponentId override
    {
      return std::same_as<T, scene::SpotLight> ? ComponentId::kSpotLight
                                               : ComponentId::kPointLight;
    }

    void Apply(scene::SceneNode& node,
      std::span<const PropertyEntry> entries) override
    {
      scene::LightValidationError error;
      const bool accepted = node.EditLight<T>(
        [&](T& light) -> bool {
          for (const auto& entry : entries) {
            if (!ApplyEntry(light, entry)) {
              error = {
                .field = std::to_string(entry.field),
                .message = "Invalid light property encoding",
              };
              return false;
            }
          }
          return true;
        },
        &error);
      if (!accepted) {
        throw std::invalid_argument(error.field + ": " + error.message);
      }
    }

  private:
    static auto ApplyEntry(T& light, const PropertyEntry& entry) -> bool
    {
      const auto field = static_cast<LocalLightField>(entry.field);
      auto& common = light.Common();
      switch (field) {
      case LocalLightField::kAffectsWorld:
        common.affects_world = entry.Bool();
        return true;
      case LocalLightField::kCastsShadows:
        common.casts_shadows = entry.Bool();
        return true;
      case LocalLightField::kContactShadows:
        common.shadow.contact_shadows = entry.Bool();
        return true;
      case LocalLightField::kShadowResolutionHint: {
        const auto hint = entry.Int32();
        if (hint < 0 || hint > 3) {
          return false;
        }
        common.shadow.resolution_hint
          = static_cast<scene::ShadowResolutionHint>(hint);
        return true;
      }
      default:
        break;
      }
      const float value = entry.Float();
      if (!std::isfinite(value)) {
        return false;
      }
      switch (field) {
      case LocalLightField::kColorR:
        common.color_rgb.x = value;
        return true;
      case LocalLightField::kColorG:
        common.color_rgb.y = value;
        return true;
      case LocalLightField::kColorB:
        common.color_rgb.z = value;
        return true;
      case LocalLightField::kShadowBias:
        common.shadow.bias = value;
        return true;
      case LocalLightField::kShadowNormalBias:
        common.shadow.normal_bias = value;
        return true;
      case LocalLightField::kExposureCompensation:
        common.exposure_compensation_ev = value;
        return true;
      case LocalLightField::kLuminousFluxLm:
        light.SetLuminousFluxLm(value);
        return true;
      case LocalLightField::kRange:
        light.SetRange(value);
        return true;
      case LocalLightField::kSourceRadius:
        light.SetSourceRadius(value);
        return true;
      case LocalLightField::kInnerConeAngleRadians:
        if constexpr (std::same_as<T, scene::SpotLight>) {
          light.SetInnerConeAngleRadians(value);
          return true;
        } else {
          return false;
        }
      case LocalLightField::kOuterConeAngleRadians:
        if constexpr (std::same_as<T, scene::SpotLight>) {
          light.SetOuterConeAngleRadians(value);
          return true;
        } else {
          return false;
        }
      default:
        return false;
      }
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

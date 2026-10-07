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

  //! Field ids shared by point and spot lights; common ids match directional.
  enum class LocalLightField : std::uint16_t {
    kColorR = 0,
    kColorG = 1,
    kColorB = 2,
    kAffectsWorld = 3,
    kCastsShadows = 5,
    kShadowBias = 6,
    kShadowNormalBias = 7,
    kContactShadows = 8,
    kShadowResolutionHint = 9,
    kExposureCompensation = 10,
    kLuminousFluxLm = 11,
    kRange = 12,
    kSourceRadius = 13,
    kInnerConeAngleRadians = 14,
    kOuterConeAngleRadians = 15,
    kCount = 16,
  };

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
      const float value = entry.value;
      if (!std::isfinite(value)) {
        return false;
      }
      const bool is_flag = value == 0.0F || value == 1.0F;
      auto& common = light.Common();
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
      case LocalLightField::kAffectsWorld:
        common.affects_world = value != 0.0F;
        return is_flag;
      case LocalLightField::kCastsShadows:
        common.casts_shadows = value != 0.0F;
        return is_flag;
      case LocalLightField::kShadowBias:
        common.shadow.bias = value;
        return true;
      case LocalLightField::kShadowNormalBias:
        common.shadow.normal_bias = value;
        return true;
      case LocalLightField::kContactShadows:
        common.shadow.contact_shadows = value != 0.0F;
        return is_flag;
      case LocalLightField::kShadowResolutionHint:
        if (value < 0.0F || value > 3.0F || std::floor(value) != value) {
          return false;
        }
        common.shadow.resolution_hint
          = static_cast<scene::ShadowResolutionHint>(
            static_cast<unsigned>(value));
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

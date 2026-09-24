//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include <Commands/IComponentPropertyApplier.h>
#include <Commands/PropertyKeys.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  enum class DirectionalLightField : std::uint16_t {
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
    kIntensityLux = 11,
    kAngularSizeRadians = 12,
    kCascadeCount = 15,
    kSplitMode = 16,
    kMaxShadowDistance = 17,
    kCascadeDistance0 = 18,
    kCascadeDistance1 = 19,
    kCascadeDistance2 = 20,
    kCascadeDistance3 = 21,
    kDistributionExponent = 22,
    kTransitionFraction = 23,
    kDistanceFadeoutFraction = 24,
    kAtmosphereLightSlot = 25,
    kUsePerPixelAtmosphereTransmittance = 26,
    kDiskScaleR = 27,
    kDiskScaleG = 28,
    kDiskScaleB = 29,
    kCount = 30,
  };

  //! Applies one complete, validated light candidate without clamping or partial writes.
  class DirectionalLightPropertyApplier final : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept -> ComponentId override
    { return ComponentId::kDirectionalLight; }

    void Apply(scene::SceneNode& node, std::span<const PropertyEntry> entries) override
    {
      scene::LightValidationError error;
      const bool accepted = node.EditLight<scene::DirectionalLight>([&](auto& light) -> bool {
        auto& common = light.Common();
        auto& csm = light.CascadedShadows();
        auto disk = light.GetAtmosphereDiskLuminanceScale();
        for (const auto& entry : entries) {
          const auto field = static_cast<DirectionalLightField>(entry.field);
          const float value = entry.value;
          if (!ValidEncoding(field, value)) {
            error = { .field = std::to_string(entry.field), .message = "Invalid light property encoding" };
            return false;
          }
          switch (field) {
          case DirectionalLightField::kColorR: common.color_rgb.x = value; break;
          case DirectionalLightField::kColorG: common.color_rgb.y = value; break;
          case DirectionalLightField::kColorB: common.color_rgb.z = value; break;
          case DirectionalLightField::kAffectsWorld: common.affects_world = value != 0.0F; break;
          case DirectionalLightField::kCastsShadows: common.casts_shadows = value != 0.0F; break;
          case DirectionalLightField::kShadowBias: common.shadow.bias = value; break;
          case DirectionalLightField::kShadowNormalBias: common.shadow.normal_bias = value; break;
          case DirectionalLightField::kContactShadows: common.shadow.contact_shadows = value != 0.0F; break;
          case DirectionalLightField::kShadowResolutionHint: common.shadow.resolution_hint = static_cast<scene::ShadowResolutionHint>(static_cast<unsigned>(value)); break;
          case DirectionalLightField::kExposureCompensation: common.exposure_compensation_ev = value; break;
          case DirectionalLightField::kIntensityLux: light.SetIntensityLux(value); break;
          case DirectionalLightField::kAngularSizeRadians: light.SetAngularSizeRadians(value); break;
          case DirectionalLightField::kCascadeCount: csm.cascade_count = static_cast<std::uint32_t>(value); break;
          case DirectionalLightField::kSplitMode: csm.split_mode = static_cast<scene::DirectionalCsmSplitMode>(static_cast<unsigned>(value)); break;
          case DirectionalLightField::kMaxShadowDistance: csm.max_shadow_distance = value; break;
          case DirectionalLightField::kCascadeDistance0: csm.cascade_distances[0] = value; break;
          case DirectionalLightField::kCascadeDistance1: csm.cascade_distances[1] = value; break;
          case DirectionalLightField::kCascadeDistance2: csm.cascade_distances[2] = value; break;
          case DirectionalLightField::kCascadeDistance3: csm.cascade_distances[3] = value; break;
          case DirectionalLightField::kDistributionExponent: csm.distribution_exponent = value; break;
          case DirectionalLightField::kTransitionFraction: csm.transition_fraction = value; break;
          case DirectionalLightField::kDistanceFadeoutFraction: csm.distance_fadeout_fraction = value; break;
          case DirectionalLightField::kAtmosphereLightSlot: light.SetAtmosphereLightSlot(static_cast<scene::AtmosphereLightSlot>(static_cast<unsigned>(value))); break;
          case DirectionalLightField::kUsePerPixelAtmosphereTransmittance: light.SetUsePerPixelAtmosphereTransmittance(value != 0.0F); break;
          case DirectionalLightField::kDiskScaleR: disk.x = value; break;
          case DirectionalLightField::kDiskScaleG: disk.y = value; break;
          case DirectionalLightField::kDiskScaleB: disk.z = value; break;
          default:
            error = { .field = std::to_string(entry.field), .message = "Unknown or removed light property" };
            return false;
          }
        }
        light.SetAtmosphereDiskLuminanceScale(disk);
        return true;
      }, &error);
      if (!accepted) throw std::invalid_argument(error.field + ": " + error.message);
    }

  private:
    static auto ValidEncoding(DirectionalLightField field, float value) -> bool
    {
      if (!std::isfinite(value)) return false;
      switch (field) {
      case DirectionalLightField::kAffectsWorld:
      case DirectionalLightField::kCastsShadows:
      case DirectionalLightField::kContactShadows:
      case DirectionalLightField::kUsePerPixelAtmosphereTransmittance:
        return value == 0.0F || value == 1.0F;
      case DirectionalLightField::kShadowResolutionHint:
        return value >= 0.0F && value <= 3.0F && std::floor(value) == value;
      case DirectionalLightField::kAtmosphereLightSlot:
        return value >= 0.0F && value <= 2.0F && std::floor(value) == value;
      case DirectionalLightField::kCascadeCount:
        return value >= 1.0F && value <= 4.0F && std::floor(value) == value;
      case DirectionalLightField::kSplitMode:
        return value == 0.0F || value == 1.0F;
      default: return true;
      }
    }
  };
} // namespace oxygen::interop::module

#pragma managed(pop)

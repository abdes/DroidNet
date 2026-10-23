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
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

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
          if (!ValidEncoding(field, entry)) {
            error = { .field = std::to_string(entry.field), .message = "Invalid light property encoding" };
            return false;
          }
          switch (field) {
          case DirectionalLightField::kColorR: common.color_rgb.x = entry.Float(); break;
          case DirectionalLightField::kColorG: common.color_rgb.y = entry.Float(); break;
          case DirectionalLightField::kColorB: common.color_rgb.z = entry.Float(); break;
          case DirectionalLightField::kAffectsWorld: common.affects_world = entry.Bool(); break;
          case DirectionalLightField::kCastsShadows: common.casts_shadows = entry.Bool(); break;
          case DirectionalLightField::kShadowBias: common.shadow.bias = entry.Float(); break;
          case DirectionalLightField::kShadowNormalBias: common.shadow.normal_bias = entry.Float(); break;
          case DirectionalLightField::kContactShadows: common.shadow.contact_shadows = entry.Bool(); break;
          case DirectionalLightField::kShadowResolutionHint: common.shadow.resolution_hint = static_cast<scene::ShadowResolutionHint>(entry.Int32()); break;
          case DirectionalLightField::kExposureCompensation: common.exposure_compensation_ev = entry.Float(); break;
          case DirectionalLightField::kIntensityLux: light.SetIntensityLux(entry.Float()); break;
          case DirectionalLightField::kAngularSizeRadians: light.SetAngularSizeRadians(entry.Float()); break;
          case DirectionalLightField::kCascadeCount: csm.cascade_count = static_cast<std::uint32_t>(entry.Int32()); break;
          case DirectionalLightField::kSplitMode: csm.split_mode = static_cast<scene::DirectionalCsmSplitMode>(entry.Int32()); break;
          case DirectionalLightField::kMaxShadowDistance: csm.max_shadow_distance = entry.Float(); break;
          case DirectionalLightField::kCascadeDistance0: csm.cascade_distances[0] = entry.Float(); break;
          case DirectionalLightField::kCascadeDistance1: csm.cascade_distances[1] = entry.Float(); break;
          case DirectionalLightField::kCascadeDistance2: csm.cascade_distances[2] = entry.Float(); break;
          case DirectionalLightField::kCascadeDistance3: csm.cascade_distances[3] = entry.Float(); break;
          case DirectionalLightField::kDistributionExponent: csm.distribution_exponent = entry.Float(); break;
          case DirectionalLightField::kTransitionFraction: csm.transition_fraction = entry.Float(); break;
          case DirectionalLightField::kDistanceFadeoutFraction: csm.distance_fadeout_fraction = entry.Float(); break;
          case DirectionalLightField::kAtmosphereLightSlot: light.SetAtmosphereLightSlot(static_cast<scene::AtmosphereLightSlot>(entry.Int32())); break;
          case DirectionalLightField::kUsePerPixelAtmosphereTransmittance: light.SetUsePerPixelAtmosphereTransmittance(entry.Bool()); break;
          case DirectionalLightField::kDiskScaleR: disk.x = entry.Float(); break;
          case DirectionalLightField::kDiskScaleG: disk.y = entry.Float(); break;
          case DirectionalLightField::kDiskScaleB: disk.z = entry.Float(); break;
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
    // A value the light accepts: a finite float, or an int32 inside its
    // enumeration or count. A bool field is valid as it is.
    static auto ValidEncoding(DirectionalLightField field,
      const PropertyEntry& entry) -> bool
    {
      const auto type = FindFieldType(field);
      if (!type) return false;
      switch (*type) {
      case FieldType::kBool: return true;
      case FieldType::kFloat: return std::isfinite(entry.Float());
      case FieldType::kInt32: break;
      }
      const auto value = entry.Int32();
      switch (field) {
      case DirectionalLightField::kShadowResolutionHint: return value >= 0 && value <= 3;
      case DirectionalLightField::kAtmosphereLightSlot: return value >= 0 && value <= 2;
      case DirectionalLightField::kCascadeCount: return value >= 1 && value <= 4;
      case DirectionalLightField::kSplitMode: return value == 0 || value == 1;
      default: return false;
      }
    }
  };
} // namespace oxygen::interop::module

#pragma managed(pop)

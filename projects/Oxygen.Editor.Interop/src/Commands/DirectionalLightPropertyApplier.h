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
    kMobility = 4,
    kCastsShadows = 5,
    kShadowBias = 6,
    kShadowNormalBias = 7,
    kContactShadows = 8,
    kShadowResolutionHint = 9,
    kExposureCompensation = 10,
    kIntensityLux = 11,
    kAngularSizeRadians = 12,
    kEnvironmentContribution = 13,
    kIsSunLight = 14,
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
    kCount,
  };

  //! Applies §5.3 property entries to `oxygen::scene::DirectionalLight`.
  class DirectionalLightPropertyApplier final
    : public IComponentPropertyApplier {
  public:
    [[nodiscard]] auto GetComponentId() const noexcept
      -> ComponentId override
    {
      return ComponentId::kDirectionalLight;
    }

    void Apply(oxygen::scene::SceneNode& node,
      std::span<const PropertyEntry> entries) override
    {
      auto light_ref = node.GetLightAs<oxygen::scene::DirectionalLight>();
      if (!light_ref) {
        LOG_F(WARNING,
          "DirectionalLightPropertyApplier skipped {} entries: node has no "
          "DirectionalLight",
          entries.size());
        return;
      }

      auto& light = light_ref->get();
      auto csm = light.CascadedShadows();
      auto csm_dirty = false;

      for (const auto& entry : entries) {
        if (!std::isfinite(entry.value)) {
          LOG_F(WARNING,
            "DirectionalLightPropertyApplier skipped field {}: non-finite "
            "value {}",
            entry.field, entry.value);
          continue;
        }

        ApplyField(light, csm, csm_dirty,
          static_cast<DirectionalLightField>(entry.field), entry.value);
      }

      if (csm_dirty) {
        light.CascadedShadows()
          = oxygen::scene::CanonicalizeCascadedShadowSettings(csm);
      }

      ApplyAtmosphereRole(light);
    }

  private:
    static auto ToBool(const float value) noexcept -> bool
    {
      return value >= 0.5F;
    }

    static auto ToUInt(const float value) noexcept -> std::uint32_t
    {
      return static_cast<std::uint32_t>(std::max(0L, std::lround(value)));
    }

    static void ApplyField(oxygen::scene::DirectionalLight& light,
      oxygen::scene::CascadedShadowSettings& csm, bool& csm_dirty,
      const DirectionalLightField field, const float value)
    {
      auto& common = light.Common();
      switch (field) {
      case DirectionalLightField::kColorR:
        common.color_rgb.x = std::clamp(value, 0.0F, 1.0F);
        break;
      case DirectionalLightField::kColorG:
        common.color_rgb.y = std::clamp(value, 0.0F, 1.0F);
        break;
      case DirectionalLightField::kColorB:
        common.color_rgb.z = std::clamp(value, 0.0F, 1.0F);
        break;
      case DirectionalLightField::kAffectsWorld:
        common.affects_world = ToBool(value);
        break;
      case DirectionalLightField::kMobility:
        common.mobility
          = static_cast<oxygen::scene::LightMobility>(ToUInt(value));
        break;
      case DirectionalLightField::kCastsShadows:
        common.casts_shadows = ToBool(value);
        break;
      case DirectionalLightField::kShadowBias:
        common.shadow.bias = value;
        break;
      case DirectionalLightField::kShadowNormalBias:
        common.shadow.normal_bias = std::max(0.0F, value);
        break;
      case DirectionalLightField::kContactShadows:
        common.shadow.contact_shadows = ToBool(value);
        break;
      case DirectionalLightField::kShadowResolutionHint:
        common.shadow.resolution_hint
          = static_cast<oxygen::scene::ShadowResolutionHint>(ToUInt(value));
        break;
      case DirectionalLightField::kExposureCompensation:
        common.exposure_compensation_ev = value;
        break;
      case DirectionalLightField::kIntensityLux:
        light.SetIntensityLux(std::max(0.0F, value));
        break;
      case DirectionalLightField::kAngularSizeRadians:
        light.SetAngularSizeRadians(std::max(0.0F, value));
        break;
      case DirectionalLightField::kEnvironmentContribution:
        light.SetEnvironmentContribution(ToBool(value));
        break;
      case DirectionalLightField::kIsSunLight:
        light.SetIsSunLight(ToBool(value));
        break;
      case DirectionalLightField::kCascadeCount:
        csm.cascade_count = ToUInt(value);
        csm_dirty = true;
        break;
      case DirectionalLightField::kSplitMode:
        csm.split_mode
          = static_cast<oxygen::scene::DirectionalCsmSplitMode>(
            ToUInt(value));
        csm_dirty = true;
        break;
      case DirectionalLightField::kMaxShadowDistance:
        csm.max_shadow_distance = value;
        csm_dirty = true;
        break;
      case DirectionalLightField::kCascadeDistance0:
        csm.cascade_distances[0] = value;
        csm_dirty = true;
        break;
      case DirectionalLightField::kCascadeDistance1:
        csm.cascade_distances[1] = value;
        csm_dirty = true;
        break;
      case DirectionalLightField::kCascadeDistance2:
        csm.cascade_distances[2] = value;
        csm_dirty = true;
        break;
      case DirectionalLightField::kCascadeDistance3:
        csm.cascade_distances[3] = value;
        csm_dirty = true;
        break;
      case DirectionalLightField::kDistributionExponent:
        csm.distribution_exponent = value;
        csm_dirty = true;
        break;
      case DirectionalLightField::kTransitionFraction:
        csm.transition_fraction = value;
        csm_dirty = true;
        break;
      case DirectionalLightField::kDistanceFadeoutFraction:
        csm.distance_fadeout_fraction = value;
        csm_dirty = true;
        break;
      default:
        LOG_F(WARNING,
          "DirectionalLightPropertyApplier skipped unknown field id {}",
          static_cast<std::uint16_t>(field));
        break;
      }
    }

    static void ApplyAtmosphereRole(oxygen::scene::DirectionalLight& light)
    {
      if (light.GetEnvironmentContribution() && light.IsSunLight()) {
        light.SetAtmosphereLightSlot(
          oxygen::scene::AtmosphereLightSlot::kPrimary);
        light.SetUsePerPixelAtmosphereTransmittance(true);
        light.SetAtmosphereDiskLuminanceScale(
          { 1.0F, 0.95F, 0.9F, 1.0F });
        return;
      }

      light.SetAtmosphereLightSlot(oxygen::scene::AtmosphereLightSlot::kNone);
      light.SetUsePerPixelAtmosphereTransmittance(false);
      light.SetAtmosphereDiskLuminanceScale(
        { 1.0F, 1.0F, 1.0F, 1.0F });
    }
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

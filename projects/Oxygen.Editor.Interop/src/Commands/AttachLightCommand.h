//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

#include <Commands/PropertyKeys.h>
#include <Commands/SchemaEnums.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/LightValidation.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  //! The authored values every light has, as the editor sends them.
  struct LightCommonValues {
    Vec3 color;
    bool affects_world;
    bool casts_shadows;
    float shadow_bias;
    float shadow_normal_bias;
    bool contact_shadows;
    std::int32_t shadow_resolution_hint;
    float exposure_compensation;
  };

  //! The authored values of a directional light, as the editor sends them.
  struct DirectionalLightValues {
    LightCommonValues common;
    float intensity_lux;
    float angular_size_radians;
    std::int32_t atmosphere_light_slot;
    bool use_per_pixel_atmosphere_transmittance;
    Vec3 disk_luminance_scale;
    std::int32_t cascade_count;
    std::int32_t split_mode;
    float max_shadow_distance;
    std::array<float, scene::kMaxShadowCascades> cascade_distances;
    float distribution_exponent;
    float transition_fraction;
    float distance_fadeout_fraction;
  };

  //! The authored values of a point light, as the editor sends them.
  struct PointLightValues {
    LightCommonValues common;
    float luminous_flux_lm;
    float range;
    float source_radius;
  };

  //! The authored values of a spot light, as the editor sends them.
  struct SpotLightValues {
    PointLightValues local;
    float inner_cone_angle_radians;
    float outer_cone_angle_radians;
  };

  namespace detail {

    //! The common properties of a light of `component`, whose fields are
    //! `TField`, each value checked against its field.
    template <typename TField>
    [[nodiscard]] auto MakeLightCommon(const ComponentId component,
      const LightCommonValues& values) -> scene::CommonLightProperties
    {
      RequireInRange(component, TField::kColorR, values.color.x);
      RequireInRange(component, TField::kColorG, values.color.y);
      RequireInRange(component, TField::kColorB, values.color.z);
      RequireInRange(component, TField::kAffectsWorld, values.affects_world);
      RequireInRange(component, TField::kCastsShadows, values.casts_shadows);
      RequireInRange(component, TField::kShadowBias, values.shadow_bias);
      RequireInRange(
        component, TField::kShadowNormalBias, values.shadow_normal_bias);
      RequireInRange(
        component, TField::kContactShadows, values.contact_shadows);
      RequireInRange(component, TField::kShadowResolutionHint,
        values.shadow_resolution_hint);
      RequireInRange(
        component, TField::kExposureCompensation, values.exposure_compensation);

      scene::CommonLightProperties common;
      common.color_rgb = values.color;
      common.affects_world = values.affects_world;
      common.casts_shadows = values.casts_shadows;
      common.shadow = {
        .bias = values.shadow_bias,
        .normal_bias = values.shadow_normal_bias,
        .contact_shadows = values.contact_shadows,
        .resolution_hint = ValidatedMember(
          ToShadowResolutionHint(values.shadow_resolution_hint)),
      };
      common.exposure_compensation_ev = values.exposure_compensation;
      return common;
    }

    //! Checks the rules between the fields of a complete detached light.
    inline auto RequireValidLight(const Component& light) -> void
    {
      if (const auto error = scene::ValidateLight(light)) {
        throw std::invalid_argument(error->field + ": " + error->message);
      }
    }

    //! The point or spot light `values` describe, with every single field
    //! checked against `component`.
    template <typename TLight>
    [[nodiscard]] auto MakeLocalLight(const ComponentId component,
      const PointLightValues& values) -> std::unique_ptr<TLight>
    {
      RequireInRange(
        component, LocalLightField::kLuminousFluxLm, values.luminous_flux_lm);
      RequireInRange(component, LocalLightField::kRange, values.range);
      RequireInRange(
        component, LocalLightField::kSourceRadius, values.source_radius);
      auto light = std::make_unique<TLight>();
      light->Common()
        = MakeLightCommon<LocalLightField>(component, values.common);
      light->SetLuminousFluxLm(values.luminous_flux_lm);
      light->SetRange(values.range);
      light->SetSourceRadius(values.source_radius);
      return light;
    }

  } // namespace detail

  //! Builds a detached directional light from authored values.
  /*!
   Each value is checked against its field (`IsInRange`), then the light
   against the rules between its fields (`scene::ValidateLight`).

   \throws std::invalid_argument when a value or the light is not valid.
  */
  [[nodiscard]] inline auto MakeDirectionalLight(
    const DirectionalLightValues& values)
    -> std::unique_ptr<scene::DirectionalLight>
  {
    constexpr auto kComponent = ComponentId::kDirectionalLight;
    using Field = DirectionalLightField;
    RequireInRange(kComponent, Field::kIntensityLux, values.intensity_lux);
    RequireInRange(
      kComponent, Field::kAngularSizeRadians, values.angular_size_radians);
    RequireInRange(
      kComponent, Field::kAtmosphereLightSlot, values.atmosphere_light_slot);
    RequireInRange(kComponent, Field::kUsePerPixelAtmosphereTransmittance,
      values.use_per_pixel_atmosphere_transmittance);
    RequireInRange(
      kComponent, Field::kDiskScaleR, values.disk_luminance_scale.x);
    RequireInRange(
      kComponent, Field::kDiskScaleG, values.disk_luminance_scale.y);
    RequireInRange(
      kComponent, Field::kDiskScaleB, values.disk_luminance_scale.z);
    RequireInRange(kComponent, Field::kCascadeCount, values.cascade_count);
    RequireInRange(kComponent, Field::kSplitMode, values.split_mode);
    RequireInRange(
      kComponent, Field::kMaxShadowDistance, values.max_shadow_distance);
    RequireInRange(
      kComponent, Field::kCascadeDistance0, values.cascade_distances[0]);
    RequireInRange(
      kComponent, Field::kCascadeDistance1, values.cascade_distances[1]);
    RequireInRange(
      kComponent, Field::kCascadeDistance2, values.cascade_distances[2]);
    RequireInRange(
      kComponent, Field::kCascadeDistance3, values.cascade_distances[3]);
    RequireInRange(
      kComponent, Field::kDistributionExponent, values.distribution_exponent);
    RequireInRange(
      kComponent, Field::kTransitionFraction, values.transition_fraction);
    RequireInRange(kComponent, Field::kDistanceFadeoutFraction,
      values.distance_fadeout_fraction);

    auto light = std::make_unique<scene::DirectionalLight>();
    light->Common() = detail::MakeLightCommon<Field>(kComponent, values.common);
    light->SetIntensityLux(values.intensity_lux);
    light->SetAngularSizeRadians(values.angular_size_radians);
    light->SetAtmosphereLightSlot(
      ValidatedMember(ToAtmosphereLightSlot(values.atmosphere_light_slot)));
    light->SetUsePerPixelAtmosphereTransmittance(
      values.use_per_pixel_atmosphere_transmittance);
    light->SetAtmosphereDiskLuminanceScale(values.disk_luminance_scale);
    light->CascadedShadows() = {
      .cascade_count = static_cast<std::uint32_t>(values.cascade_count),
      .split_mode
      = ValidatedMember(ToDirectionalCsmSplitMode(values.split_mode)),
      .max_shadow_distance = values.max_shadow_distance,
      .cascade_distances = values.cascade_distances,
      .distribution_exponent = values.distribution_exponent,
      .transition_fraction = values.transition_fraction,
      .distance_fadeout_fraction = values.distance_fadeout_fraction,
    };
    detail::RequireValidLight(*light);
    return light;
  }

  //! Builds a detached point light from authored values.
  /*!
   \throws std::invalid_argument when a value or the light is not valid.
  */
  [[nodiscard]] inline auto MakePointLight(const PointLightValues& values)
    -> std::unique_ptr<scene::PointLight>
  {
    auto light = detail::MakeLocalLight<scene::PointLight>(
      ComponentId::kPointLight, values);
    detail::RequireValidLight(*light);
    return light;
  }

  //! Builds a detached spot light from authored values.
  /*!
   \throws std::invalid_argument when a value or the light is not valid.
  */
  [[nodiscard]] inline auto MakeSpotLight(const SpotLightValues& values)
    -> std::unique_ptr<scene::SpotLight>
  {
    constexpr auto kComponent = ComponentId::kSpotLight;
    RequireInRange(kComponent, LocalLightField::kInnerConeAngleRadians,
      values.inner_cone_angle_radians);
    RequireInRange(kComponent, LocalLightField::kOuterConeAngleRadians,
      values.outer_cone_angle_radians);
    auto light
      = detail::MakeLocalLight<scene::SpotLight>(kComponent, values.local);
    light->SetConeAnglesRadians(
      values.inner_cone_angle_radians, values.outer_cone_angle_radians);
    detail::RequireValidLight(*light);
    return light;
  }

  //! Owns a complete detached light until the scene mutation phase accepts it.
  class AttachLightCommand final : public EditorCommand {
  public:
    AttachLightCommand(scene::NodeHandle node, std::unique_ptr<Component> light)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
      , light_(std::move(light))
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) return;
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) return;
      scene::LightValidationError error;
      if (!node->ReplaceLight(std::move(light_), &error)) {
        throw std::invalid_argument(error.field + ": " + error.message);
      }
      context.Scene->Update(false);
      context.Scene->SyncObservers();
      context.Scene->NotifyEnvironmentAuthoringChange();
    }

  private:
    scene::NodeHandle node_;
    std::unique_ptr<Component> light_;
  };

  class DetachLightCommand final : public EditorCommand {
  public:
    explicit DetachLightCommand(scene::NodeHandle node)
      : EditorCommand(core::PhaseId::kSceneMutation)
      , node_(node)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) return;
      auto node = context.Scene->GetNode(node_);
      if (!node || !node->IsAlive()) return;
      const bool detached = node->DetachLight();
      context.Scene->Update(false);
      context.Scene->SyncObservers();
      if (detached) context.Scene->NotifyEnvironmentAuthoringChange();
    }

  private:
    scene::NodeHandle node_;
  };
} // namespace oxygen::interop::module

#pragma managed(pop)

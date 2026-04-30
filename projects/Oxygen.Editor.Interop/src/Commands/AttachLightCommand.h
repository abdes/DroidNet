//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  enum class LightKind {
    kDirectional,
    kPoint,
    kSpot,
  };

  struct LightCommonParams {
    oxygen::Vec3 color { 1.0F, 1.0F, 1.0F };
    bool affects_world = true;
    oxygen::scene::LightMobility mobility
      = oxygen::scene::LightMobility::kRealtime;
    bool casts_shadows = false;
    oxygen::scene::ShadowSettings shadow {};
    float exposure_compensation_ev = 0.0F;
  };

  struct DirectionalLightParams {
    std::uint32_t cascade_count = oxygen::scene::kMaxShadowCascades;
    oxygen::scene::DirectionalCsmSplitMode split_mode
      = oxygen::scene::DirectionalCsmSplitMode::kGenerated;
    float max_shadow_distance
      = oxygen::scene::kDefaultDirectionalMaxShadowDistance;
    std::array<float, oxygen::scene::kMaxShadowCascades> cascade_distances
      = oxygen::scene::kDefaultDirectionalCascadeDistances;
    float distribution_exponent
      = oxygen::scene::kDefaultDirectionalDistributionExponent;
    float transition_fraction
      = oxygen::scene::kDefaultDirectionalTransitionFraction;
    float distance_fadeout_fraction
      = oxygen::scene::kDefaultDirectionalDistanceFadeoutFraction;
  };

  class AttachLightCommand final : public EditorCommand {
  public:
    AttachLightCommand(oxygen::scene::NodeHandle node, LightKind kind,
      LightCommonParams common, float intensity, float range,
      float inner_cone_angle, float outer_cone_angle,
      float source_radius, float decay_exponent, float angular_size,
      bool environment_contribution, bool is_sun_light,
      DirectionalLightParams directional = {})
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
      , kind_(kind)
      , common_(common)
      , intensity_(intensity)
      , range_(range)
      , inner_cone_angle_(inner_cone_angle)
      , outer_cone_angle_(outer_cone_angle)
      , source_radius_(source_radius)
      , decay_exponent_(decay_exponent)
      , angular_size_(angular_size)
      , environment_contribution_(environment_contribution)
      , is_sun_light_(is_sun_light)
      , directional_(directional)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }

      const auto scene_node_opt = context.Scene->GetNode(node_);
      if (!scene_node_opt || !scene_node_opt->IsAlive()) {
        return;
      }

      auto scene_node = *scene_node_opt;
      switch (kind_) {
      case LightKind::kDirectional: {
        auto light = std::make_unique<oxygen::scene::DirectionalLight>();
        ApplyCommon(light->Common());
        light->SetIntensityLux(intensity_);
        light->SetAngularSizeRadians(angular_size_);
        light->SetEnvironmentContribution(environment_contribution_);
        light->SetIsSunLight(is_sun_light_);
        ApplyAtmosphereRole(*light);
        auto cascaded_shadows = oxygen::scene::CascadedShadowSettings {};
        cascaded_shadows.cascade_count = directional_.cascade_count;
        cascaded_shadows.split_mode = directional_.split_mode;
        cascaded_shadows.max_shadow_distance = directional_.max_shadow_distance;
        cascaded_shadows.cascade_distances = directional_.cascade_distances;
        cascaded_shadows.distribution_exponent
          = directional_.distribution_exponent;
        cascaded_shadows.transition_fraction = directional_.transition_fraction;
        cascaded_shadows.distance_fadeout_fraction
          = directional_.distance_fadeout_fraction;
        light->CascadedShadows()
          = oxygen::scene::CanonicalizeCascadedShadowSettings(
            cascaded_shadows);
        (void)scene_node.ReplaceLight(std::move(light));
        break;
      }
      case LightKind::kPoint: {
        auto light = std::make_unique<oxygen::scene::PointLight>();
        ApplyCommon(light->Common());
        light->SetLuminousFluxLm(intensity_);
        light->SetRange(range_);
        light->SetSourceRadius(source_radius_);
        light->SetDecayExponent(decay_exponent_);
        (void)scene_node.ReplaceLight(std::move(light));
        break;
      }
      case LightKind::kSpot: {
        auto light = std::make_unique<oxygen::scene::SpotLight>();
        ApplyCommon(light->Common());
        light->SetLuminousFluxLm(intensity_);
        light->SetRange(range_);
        light->SetSourceRadius(source_radius_);
        light->SetDecayExponent(decay_exponent_);
        light->SetInnerConeAngleRadians(inner_cone_angle_);
        light->SetOuterConeAngleRadians(outer_cone_angle_);
        (void)scene_node.ReplaceLight(std::move(light));
        break;
      }
      }

      context.Scene->GetDirectionalLightResolver().OnLightChanged(node_);
      context.Scene->Update(false);
      context.Scene->SyncObservers();
    }

  private:
    void ApplyCommon(oxygen::scene::CommonLightProperties& common) const
    {
      common.affects_world = common_.affects_world;
      common.color_rgb = common_.color;
      common.mobility = common_.mobility;
      common.casts_shadows = common_.casts_shadows;
      common.shadow = common_.shadow;
      common.exposure_compensation_ev = common_.exposure_compensation_ev;
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

    oxygen::scene::NodeHandle node_;
    LightKind kind_;
    LightCommonParams common_;
    float intensity_ = 0.0F;
    float range_ = 0.0F;
    float inner_cone_angle_ = 0.0F;
    float outer_cone_angle_ = 0.0F;
    float source_radius_ = 0.0F;
    float decay_exponent_ = 2.0F;
    float angular_size_ = 0.0F;
    bool environment_contribution_ = false;
    bool is_sun_light_ = false;
    DirectionalLightParams directional_ {};
  };

  class DetachLightCommand final : public EditorCommand {
  public:
    explicit DetachLightCommand(oxygen::scene::NodeHandle node)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , node_(node)
    {
    }

    void Execute(CommandContext& context) override
    {
      if (!context.Scene) {
        return;
      }

      const auto scene_node_opt = context.Scene->GetNode(node_);
      if (!scene_node_opt || !scene_node_opt->IsAlive()) {
        return;
      }

      auto scene_node = *scene_node_opt;
      (void)scene_node.DetachLight();
      context.Scene->GetDirectionalLightResolver().OnLightChanged(node_);
      context.Scene->Update(false);
      context.Scene->SyncObservers();
    }

  private:
    oxygen::scene::NodeHandle node_;
  };

} // namespace oxygen::interop::module

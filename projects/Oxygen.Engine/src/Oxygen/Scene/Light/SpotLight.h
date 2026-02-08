//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

//! Spot light component for scene nodes.
/*!
 Represents a local cone light emitted from the owning node's world position.
 The light direction is derived from the owning node's transform.

 The component stores authored properties and caches a pointer to the owning
 node's TransformComponent via the Composition dependency mechanism.
*/
class SpotLight final : public Component {
  OXYGEN_COMPONENT(SpotLight)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  //! Creates a default spot light.
  SpotLight() = default;

  //! Virtual destructor.
  ~SpotLight() override = default;

  OXYGEN_DEFAULT_COPYABLE(SpotLight)
  OXYGEN_DEFAULT_MOVABLE(SpotLight)

  //! Gets the common light properties.
  OXGN_SCN_NDAPI auto Common() noexcept -> CommonLightProperties&
  {
    return common_;
  }

  //! Gets the common light properties.
  OXGN_SCN_NDAPI auto Common() const noexcept -> const CommonLightProperties&
  {
    return common_;
  }

  //! Sets the effective range (length) of the light in world units.
  auto SetRange(const float range) noexcept -> void { range_ = range; }

  //! Gets the effective range (length) of the light in world units.
  OXGN_SCN_NDAPI auto GetRange() const noexcept -> float { return range_; }

  //! Sets the attenuation model used by shaders.
  auto SetAttenuationModel(const AttenuationModel model) noexcept -> void
  {
    attenuation_model_ = model;
  }

  //! Gets the attenuation model used by shaders.
  OXGN_SCN_NDAPI auto GetAttenuationModel() const noexcept -> AttenuationModel
  {
    return attenuation_model_;
  }

  //! Sets the custom decay exponent (used only for kCustomExponent).
  auto SetDecayExponent(const float decay_exponent) noexcept -> void
  {
    decay_exponent_ = decay_exponent;
  }

  //! Gets the custom decay exponent.
  OXGN_SCN_NDAPI auto GetDecayExponent() const noexcept -> float
  {
    return decay_exponent_;
  }

  //! Sets the inner and outer cone angles in radians.
  auto SetConeAnglesRadians(const float inner_cone_angle_radians,
    const float outer_cone_angle_radians) noexcept -> void
  {
    inner_cone_angle_radians_ = inner_cone_angle_radians;
    outer_cone_angle_radians_ = outer_cone_angle_radians;
  }

  //! Gets the inner cone angle in radians.
  OXGN_SCN_NDAPI auto GetInnerConeAngleRadians() const noexcept -> float
  {
    return inner_cone_angle_radians_;
  }

  //! Gets the outer cone angle in radians.
  OXGN_SCN_NDAPI auto GetOuterConeAngleRadians() const noexcept -> float
  {
    return outer_cone_angle_radians_;
  }

  //! Sets the source radius in world units.
  auto SetSourceRadius(const float source_radius) noexcept -> void
  {
    source_radius_ = source_radius;
  }
  //! Gets the source radius in world units.
  OXGN_SCN_NDAPI auto GetSourceRadius() const noexcept -> float
  {
    return source_radius_;
  }

  //! Sets the light's luminous flux in lumens.
  auto SetLuminousFluxLm(const float luminous_flux_lm) noexcept -> void
  {
    luminous_flux_lm_ = luminous_flux_lm;
  }

  //! Gets the light's luminous flux in lumens.
  //! Typical values: 800 lm (~60W incandescent), 1600 lm (~100W).
  OXGN_SCN_NDAPI auto GetLuminousFluxLm() const noexcept -> float
  {
    return luminous_flux_lm_;
  }

protected:
  OXGN_SCN_API auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override;

private:
  CommonLightProperties common_ {};

  //! Maximum reach of the light in world units.
  //! Scale: linear (meters).
  //! Variation: Small changes affect the falloff volume; determines shader
  //! culling.
  float range_ = 10.0F;

  AttenuationModel attenuation_model_ = AttenuationModel::kInverseSquare;
  float decay_exponent_ = 2.0F;

  //! Angle of the inner cone where attenuation starts.
  //! Scale: radians. Must be <= outer angle.
  //! Variation: Small changes affect the sharpness of the light cone's edge.
  float inner_cone_angle_radians_ = 0.4F;

  //! Angle of the outer cone where light reaches zero.
  //! Scale: radians. Must be >= inner angle.
  //! Variation: Small changes affect the overall spread of the spot.
  float outer_cone_angle_radians_ = 0.6F;

  //! Radius of the emission disk in world units.
  //! Scale: linear (meters).
  //! Variation: Small changes affect the softness of specular highlights and
  //! contact shadows.
  float source_radius_ = 0.0F;

  //! Total light power in lumens (lm).
  //! Scale: linear. Typical: 800 (60W bulb), 1600 (100W bulb).
  //! Variation: Large strides (e.g. 500+) are needed for noticeable brightness
  //! changes.
  float luminous_flux_lm_ = 800.0F;

  detail::TransformComponent* transform_ { nullptr };
};

} // namespace oxygen::scene

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

//! Directional light component for scene nodes.
/*!
 Represents a light at infinity (e.g. sun/moon). The light direction is derived
 from the owning node's transform (see `oxygen::space::move::Forward` in the
 lighting design).

 The component stores authored properties and caches a pointer to the owning
 node's TransformComponent via the Composition dependency mechanism.
*/
class DirectionalLight final : public Component {
  OXYGEN_COMPONENT(DirectionalLight)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  //! Creates a default directional light.
  DirectionalLight() = default;

  //! Virtual destructor.
  ~DirectionalLight() override = default;

  OXYGEN_DEFAULT_COPYABLE(DirectionalLight)
  OXYGEN_DEFAULT_MOVABLE(DirectionalLight)

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

  //! Sets the light's angular size in radians.
  auto SetAngularSizeRadians(const float angular_size_radians) noexcept -> void
  {
    angular_size_radians_ = angular_size_radians;
  }

  //! Gets the light's angular size in radians.
  OXGN_SCN_NDAPI auto GetAngularSizeRadians() const noexcept -> float
  {
    return angular_size_radians_;
  }

  //! Enables or disables environment contribution.
  auto SetEnvironmentContribution(const bool enabled) noexcept -> void
  {
    environment_contribution_ = enabled;
  }

  //! Returns true if this light contributes to environment systems.
  OXGN_SCN_NDAPI auto GetEnvironmentContribution() const noexcept -> bool
  {
    return environment_contribution_;
  }

  //! Designates this light as the sun for atmospheric systems.
  /*!
   When true, this directional light's direction is used by atmospheric systems
   (fog inscattering, sky atmosphere, etc.). Only the first enabled
   DirectionalLight with is_sun_light=true is used.
  */
  auto SetIsSunLight(const bool is_sun) noexcept -> void
  {
    is_sun_light_ = is_sun;
  }

  //! Returns true if this light is designated as the sun for atmosphere.
  [[nodiscard]] auto IsSunLight() const noexcept -> bool
  {
    return is_sun_light_;
  }

  //! Gets the cascaded shadow settings.
  OXGN_SCN_NDAPI auto CascadedShadows() noexcept -> CascadedShadowSettings&
  {
    return csm_;
  }

  //! Gets the cascaded shadow settings.
  OXGN_SCN_NDAPI auto CascadedShadows() const noexcept
    -> const CascadedShadowSettings&
  {
    return csm_;
  }

protected:
  OXGN_SCN_API auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override;

private:
  CommonLightProperties common_ {};
  float angular_size_radians_ = 0.0F;
  bool environment_contribution_ = false;
  bool is_sun_light_ = false;
  CascadedShadowSettings csm_ {};
  detail::TransformComponent* transform_ { nullptr };
};

} // namespace oxygen::scene

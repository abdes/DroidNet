//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene::environment {

//! Sky background source.
enum class SkySphereSource {
  //! Use a specified cubemap asset.
  kCubemap,

  //! Render a constant color background.
  kSolidColor,
};

//! Scene-global sky background parameters.
/*!
 A SkySphere is intended as a background when a procedural atmosphere is not
 present. The renderer chooses between `SkyAtmosphere` and `SkySphere`.
*/
class SkySphere final : public EnvironmentSystem {
  OXYGEN_COMPONENT(SkySphere)

public:
  //! Constructs a default sky sphere.
  SkySphere() = default;

  //! Virtual destructor.
  ~SkySphere() override = default;

  OXYGEN_DEFAULT_COPYABLE(SkySphere)
  OXYGEN_DEFAULT_MOVABLE(SkySphere)

  //! Sets the background source.
  auto SetSource(const SkySphereSource source) noexcept -> void
  {
    source_ = source;
  }

  //! Gets the background source.
  [[nodiscard]] auto GetSource() const noexcept -> SkySphereSource
  {
    return source_;
  }

  //! Sets cubemap resource key (used when source is kCubemap).
  auto SetCubemapResource(const content::ResourceKey& key) noexcept -> void
  {
    cubemap_resource_ = key;
  }

  //! Gets cubemap resource key.
  [[nodiscard]] auto GetCubemapResource() const noexcept
    -> const content::ResourceKey&
  {
    return cubemap_resource_;
  }

  //! Sets solid background color (linear RGB).
  auto SetSolidColorRgb(const Vec3& rgb) noexcept -> void
  {
    solid_color_rgb_ = rgb;
  }

  //! Gets solid background color.
  [[nodiscard]] auto GetSolidColorRgb() const noexcept -> const Vec3&
  {
    return solid_color_rgb_;
  }

  //! Sets intensity multiplier (unitless).
  auto SetIntensity(const float intensity) noexcept -> void
  {
    intensity_ = intensity;
  }

  //! Gets intensity.
  [[nodiscard]] auto GetIntensity() const noexcept -> float
  {
    return intensity_;
  }

  //! Sets an azimuth rotation around world up (radians).
  auto SetRotationRadians(const float radians) noexcept -> void
  {
    rotation_radians_ = radians;
  }

  //! Gets rotation around world up (radians).
  [[nodiscard]] auto GetRotationRadians() const noexcept -> float
  {
    return rotation_radians_;
  }

  //! Sets tint (linear RGB).
  auto SetTintRgb(const Vec3& rgb) noexcept -> void { tint_rgb_ = rgb; }

  //! Gets tint.
  [[nodiscard]] auto GetTintRgb() const noexcept -> const Vec3&
  {
    return tint_rgb_;
  }

private:
  SkySphereSource source_ = SkySphereSource::kCubemap;
  content::ResourceKey cubemap_resource_ {};
  Vec3 solid_color_rgb_ { 0.0F, 0.0F, 0.0F };

  float intensity_ = 1.0F;
  float rotation_radians_ = 0.0F;
  Vec3 tint_rgb_ { 1.0F, 1.0F, 1.0F };
};

} // namespace oxygen::scene::environment

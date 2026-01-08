//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene::environment {

//! Image-based lighting source type.
enum class SkyLightSource {
  //! The renderer captures the scene's sky/background into an IBL.
  kCapturedScene,

  //! The renderer uses a specified cubemap asset.
  kSpecifiedCubemap,
};

//! Scene-global sky light (IBL) parameters.
/*!
 A SkyLight provides ambient image-based lighting for diffuse and specular.

 This component only stores authored parameters. GPU resource creation and
 caching are renderer responsibilities.
*/
class SkyLight final : public EnvironmentSystem {
  OXYGEN_COMPONENT(SkyLight)

public:
  //! Constructs a default sky light.
  SkyLight() = default;

  //! Virtual destructor.
  ~SkyLight() override = default;

  OXYGEN_DEFAULT_COPYABLE(SkyLight)
  OXYGEN_DEFAULT_MOVABLE(SkyLight)

  //! Sets the sky light source.
  auto SetSource(const SkyLightSource source) noexcept -> void
  {
    source_ = source;
  }

  //! Gets the sky light source.
  [[nodiscard]] auto GetSource() const noexcept -> SkyLightSource
  {
    return source_;
  }

  //! Sets the cubemap asset key (used when source is kSpecifiedCubemap).
  auto SetCubemapAsset(const data::AssetKey& key) noexcept -> void
  {
    cubemap_asset_ = key;
  }

  //! Gets the cubemap asset key.
  [[nodiscard]] auto GetCubemapAsset() const noexcept -> const data::AssetKey&
  {
    return cubemap_asset_;
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

  //! Sets tint (linear RGB).
  auto SetTintRgb(const Vec3& rgb) noexcept -> void { tint_rgb_ = rgb; }

  //! Gets tint.
  [[nodiscard]] auto GetTintRgb() const noexcept -> const Vec3&
  {
    return tint_rgb_;
  }

  //! Sets diffuse contribution multiplier.
  auto SetDiffuseIntensity(const float intensity) noexcept -> void
  {
    diffuse_intensity_ = intensity;
  }

  //! Gets diffuse contribution multiplier.
  [[nodiscard]] auto GetDiffuseIntensity() const noexcept -> float
  {
    return diffuse_intensity_;
  }

  //! Sets specular contribution multiplier.
  auto SetSpecularIntensity(const float intensity) noexcept -> void
  {
    specular_intensity_ = intensity;
  }

  //! Gets specular contribution multiplier.
  [[nodiscard]] auto GetSpecularIntensity() const noexcept -> float
  {
    return specular_intensity_;
  }

private:
  SkyLightSource source_ = SkyLightSource::kCapturedScene;
  data::AssetKey cubemap_asset_ {};

  float intensity_ = 1.0F;
  Vec3 tint_rgb_ { 1.0F, 1.0F, 1.0F };

  float diffuse_intensity_ = 1.0F;
  float specular_intensity_ = 1.0F;
};

} // namespace oxygen::scene::environment

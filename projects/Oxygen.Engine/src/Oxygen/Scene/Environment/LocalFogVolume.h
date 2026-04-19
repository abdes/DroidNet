//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>

namespace oxygen::scene::environment {

//! Node-attached local fog volume authoring component.
class LocalFogVolume final : public Component {
  OXYGEN_COMPONENT(LocalFogVolume)
  OXYGEN_COMPONENT_REQUIRES(detail::TransformComponent)

public:
  static constexpr float kBaseVolumeRadiusMeters = 5.0F;

  LocalFogVolume() = default;
  ~LocalFogVolume() override = default;

  OXYGEN_DEFAULT_COPYABLE(LocalFogVolume)
  OXYGEN_DEFAULT_MOVABLE(LocalFogVolume)

  auto SetEnabled(const bool enabled) noexcept -> void { enabled_ = enabled; }
  [[nodiscard]] auto IsEnabled() const noexcept -> bool { return enabled_; }

  auto SetRadialFogExtinction(const float value) noexcept -> void
  {
    radial_fog_extinction_ = value;
  }
  [[nodiscard]] auto GetRadialFogExtinction() const noexcept -> float
  {
    return radial_fog_extinction_;
  }

  auto SetHeightFogExtinction(const float value) noexcept -> void
  {
    height_fog_extinction_ = value;
  }
  [[nodiscard]] auto GetHeightFogExtinction() const noexcept -> float
  {
    return height_fog_extinction_;
  }

  auto SetHeightFogFalloff(const float value) noexcept -> void
  {
    height_fog_falloff_ = value;
  }
  [[nodiscard]] auto GetHeightFogFalloff() const noexcept -> float
  {
    return height_fog_falloff_;
  }

  auto SetHeightFogOffset(const float value) noexcept -> void
  {
    height_fog_offset_ = value;
  }
  [[nodiscard]] auto GetHeightFogOffset() const noexcept -> float
  {
    return height_fog_offset_;
  }

  auto SetFogPhaseG(const float value) noexcept -> void { fog_phase_g_ = value; }
  [[nodiscard]] auto GetFogPhaseG() const noexcept -> float
  {
    return fog_phase_g_;
  }

  auto SetFogAlbedo(const Vec3& value) noexcept -> void { fog_albedo_ = value; }
  [[nodiscard]] auto GetFogAlbedo() const noexcept -> const Vec3&
  {
    return fog_albedo_;
  }

  auto SetFogEmissive(const Vec3& value) noexcept -> void
  {
    fog_emissive_ = value;
  }
  [[nodiscard]] auto GetFogEmissive() const noexcept -> const Vec3&
  {
    return fog_emissive_;
  }

  auto SetSortPriority(const int value) noexcept -> void { sort_priority_ = value; }
  [[nodiscard]] auto GetSortPriority() const noexcept -> int
  {
    return sort_priority_;
  }

private:
  bool enabled_ { true };
  float radial_fog_extinction_ { 1.0F };
  float height_fog_extinction_ { 1.0F };
  float height_fog_falloff_ { 1000.0F };
  float height_fog_offset_ { 0.0F };
  float fog_phase_g_ { 0.2F };
  Vec3 fog_albedo_ { 1.0F, 1.0F, 1.0F };
  Vec3 fog_emissive_ { 0.0F, 0.0F, 0.0F };
  int sort_priority_ { 0 };
};

} // namespace oxygen::scene::environment

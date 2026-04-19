//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Environment/Internal/AtmosphereLightState.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentViewProducts.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::vortex::environment::internal {

struct StableAtmosphereState {
  environment::EnvironmentViewProducts view_products {};
  std::uint32_t conventional_shadow_authority_slot {
    environment::kInvalidAtmosphereLightSlot
  };
  std::uint32_t conventional_shadow_cascade_count { 0U };
  bool conventional_shadow_authority_slot0_only { true };
  std::uint64_t authored_hash { 0U };
  std::uint64_t atmosphere_revision { 0U };
  std::uint64_t light_revision { 0U };
  std::uint64_t stable_revision { 0U };
};

class AtmosphereState {
public:
  [[nodiscard]] auto Update(const scene::Scene& scene,
    const ResolvedAtmosphereLightState& light_state) -> bool;
  auto Reset() -> void;

  [[nodiscard]] auto GetState() const noexcept -> const StableAtmosphereState&
  {
    return state_;
  }

private:
  StableAtmosphereState state_ {};
  std::uint64_t stable_hash_ { 0U };
};

} // namespace oxygen::vortex::environment::internal

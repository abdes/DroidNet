//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::vortex::environment::internal {

struct ResolvedAtmosphereLightState {
  environment::AtmosphereLightSlots atmosphere_lights {};
  std::array<scene::NodeHandle, environment::kAtmosphereLightSlotCount>
    source_nodes {};
  std::array<std::uint32_t, environment::kAtmosphereLightSlotCount>
    source_cascade_counts {};
  std::array<bool, environment::kAtmosphereLightSlotCount> explicit_slot_claims {
    false,
    false,
  };
  std::uint32_t active_light_count { 0U };
  std::uint32_t conflict_count { 0U };
  std::uint32_t first_conflict_slot { environment::kInvalidAtmosphereLightSlot };
  std::uint32_t shadow_authority_slot {
    environment::kInvalidAtmosphereLightSlot
  };
  bool shadow_authority_slot0_only { true };
  std::uint64_t authored_hash { 0U };
  std::uint64_t revision { 0U };
};

class AtmosphereLightState {
public:
  [[nodiscard]] auto Update(const scene::Scene& scene) -> bool;
  auto Reset() -> void;

  [[nodiscard]] auto GetState() const noexcept
    -> const ResolvedAtmosphereLightState&
  {
    return state_;
  }

private:
  ResolvedAtmosphereLightState state_ {};
};

} // namespace oxygen::vortex::environment::internal
